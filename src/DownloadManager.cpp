#include "BigScreen/DownloadManager.hpp"

#include <Python.h>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <unordered_set>

#include "main.hpp"
#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/QuickJsEngine.hpp"
#include "BigScreen/QuickJsPythonModule.hpp"
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace BigScreen {
    namespace {
        constexpr std::uint64_t RequiredReserve = 512ull * 1024ull * 1024ull;
        const std::filesystem::path InternalNativeRuntime{
            "/data/user/0/com.beatgames.beatsaber/code_cache/BigScreen"};

        class ScopedPythonGil final {
        public:
            ScopedPythonGil() : state_(PyGILState_Ensure()) {}
            ~ScopedPythonGil() { PyGILState_Release(state_); }
            ScopedPythonGil(const ScopedPythonGil&) = delete;
            ScopedPythonGil& operator=(const ScopedPythonGil&) = delete;

        private:
            PyGILState_STATE state_;
        };

        struct PythonObjectDeleter {
            void operator()(PyObject* object) const
            {
                Py_XDECREF(object);
            }
        };

        using PythonObject = std::unique_ptr<PyObject, PythonObjectDeleter>;

        /// Converts the currently raised Python exception, including its
        /// traceback, into text that can survive interpreter teardown and be
        /// written to Big Screen's persistent log. PyRun_SimpleString prints
        /// errors to stderr and may clear them, which previously reduced every
        /// downloader startup failure to the same unhelpful message.
        std::string TakePythonExceptionText()
        {
            PythonObject exception{PyErr_GetRaisedException()};
            if(!exception)
                return "Python did not provide an exception object.";

            PythonObject tracebackModule{PyImport_ImportModule("traceback")};
            if(tracebackModule)
            {
                PythonObject formatter{
                    PyObject_GetAttrString(tracebackModule.get(), "format_exception")};
                if(formatter)
                {
                    PythonObject lines{PyObject_CallOneArg(
                        formatter.get(), exception.get())};
                    if(lines)
                    {
                        PythonObject separator{PyUnicode_FromString("")};
                        PythonObject formatted{separator
                            ? PyUnicode_Join(separator.get(), lines.get())
                            : nullptr};
                        if(formatted)
                        {
                            const char* utf8 = PyUnicode_AsUTF8(formatted.get());
                            if(utf8) return utf8;
                        }
                    }
                }
                // Formatting is best-effort. Do not let a secondary traceback
                // formatting error hide the original exception string.
                PyErr_Clear();
            }

            PythonObject fallback{PyObject_Str(exception.get())};
            if(fallback)
            {
                const char* utf8 = PyUnicode_AsUTF8(fallback.get());
                if(utf8) return utf8;
            }
            PyErr_Clear();
            return "Python raised an exception that could not be formatted.";
        }

        PythonObject CreatePythonGlobals(
            const char* json,
            std::size_t jsonLength)
        {
            PythonObject globals{PyDict_New()};
            if(!globals)
                return {};
            if(PyDict_SetItemString(
                   globals.get(), "__builtins__", PyEval_GetBuiltins()) < 0)
                return {};
            PythonObject job{PyUnicode_FromStringAndSize(
                json, static_cast<Py_ssize_t>(jsonLength))};
            if(!job ||
               PyDict_SetItemString(globals.get(), "BIGSCREEN_JOB", job.get()) < 0)
                return {};
            return globals;
        }

        bool FilesEqual(
            const std::filesystem::path& left,
            const std::filesystem::path& right)
        {
            std::error_code error;
            const auto leftSize = std::filesystem::file_size(left, error);
            if(error) return false;
            const auto rightSize = std::filesystem::file_size(right, error);
            if(error || leftSize != rightSize) return false;

            std::ifstream leftStream(left, std::ios::binary);
            std::ifstream rightStream(right, std::ios::binary);
            if(!leftStream || !rightStream) return false;
            std::array<char, 64u * 1024u> leftBuffer{};
            std::array<char, 64u * 1024u> rightBuffer{};
            while(leftStream && rightStream)
            {
                leftStream.read(leftBuffer.data(), leftBuffer.size());
                rightStream.read(rightBuffer.data(), rightBuffer.size());
                const auto leftCount = leftStream.gcount();
                const auto rightCount = rightStream.gcount();
                if(leftCount != rightCount ||
                   !std::equal(
                       leftBuffer.begin(),
                       leftBuffer.begin() + leftCount,
                       rightBuffer.begin()))
                    return false;
            }
            return leftStream.eof() && rightStream.eof();
        }

        /// Owns the filesystem transaction that promotes a staged yt-dlp
        /// package. Any return before Accept() moves the candidate back to
        /// `next` and restores the prior active package, so an unrelated
        /// Python/native initialization error cannot strand a bad active file.
        class DownloaderActivation final {
        public:
            explicit DownloaderActivation(std::filesystem::path runtime)
                : runtime_(std::move(runtime)),
                  next_(runtime_ / "yt-dlp-next"),
                  active_(runtime_ / "yt-dlp-active"),
                  previous_(runtime_ / "yt-dlp-previous") {}

            ~DownloaderActivation()
            {
                if(attempted_ && !accepted_)
                    RestoreForRetry();
            }

            bool Promote(std::string& error)
            {
                if(!std::filesystem::is_regular_file(next_))
                    return true;
                attempted_ = true;
                hadActive_ = std::filesystem::is_regular_file(active_);
                std::ifstream version(next_.string() + ".version");
                if(version) std::getline(version, candidateVersion_);

                if(!Remove(previous_, error) ||
                   !Remove(previous_.string() + ".version", error))
                    return false;
                if(hadActive_)
                {
                    if(!Rename(active_, previous_, error))
                        return false;
                    originalMoved_ = true;
                    if(std::filesystem::is_regular_file(active_.string() + ".version") &&
                       !Rename(
                           active_.string() + ".version",
                           previous_.string() + ".version",
                           error))
                        return false;
                }
                if(!Rename(next_, active_, error))
                    return false;
                candidateActive_ = true;
                if(std::filesystem::is_regular_file(next_.string() + ".version") &&
                   !Rename(
                       next_.string() + ".version",
                       active_.string() + ".version",
                       error))
                    return false;
                return true;
            }

            bool Promoted() const { return candidateActive_; }
            const std::string& CandidateVersion() const { return candidateVersion_; }

            void Accept()
            {
                accepted_ = true;
                std::error_code ignored;
                std::filesystem::remove(
                    runtime_ / "yt-dlp-rejected.version", ignored);
            }

            bool Reject(std::string& error)
            {
                if(!candidateActive_)
                    return true;
                if(!Remove(active_, error) ||
                   !Remove(active_.string() + ".version", error))
                    return false;
                candidateActive_ = false;
                if(originalMoved_)
                {
                    if(!Rename(previous_, active_, error))
                        return false;
                    originalMoved_ = false;
                    if(std::filesystem::is_regular_file(previous_.string() + ".version") &&
                       !Rename(
                           previous_.string() + ".version",
                           active_.string() + ".version",
                           error))
                        return false;
                }
                if(!WriteRejectedVersion(candidateVersion_, error))
                    return false;
                accepted_ = true;
                return true;
            }

        private:
            bool Remove(const std::filesystem::path& path, std::string& error)
            {
                std::error_code fileError;
                std::filesystem::remove(path, fileError);
                if(!fileError) return true;
                error = "Could not remove " + path.filename().string() + ": " +
                        fileError.message();
                return false;
            }

            bool Rename(
                const std::filesystem::path& from,
                const std::filesystem::path& to,
                std::string& error)
            {
                std::error_code fileError;
                std::filesystem::rename(from, to, fileError);
                if(!fileError) return true;
                error = "Could not activate " + from.filename().string() + ": " +
                        fileError.message();
                return false;
            }

            bool WriteRejectedVersion(
                const std::string& version,
                std::string& error) const
            {
                std::ofstream rejected(
                    runtime_ / "yt-dlp-rejected.version",
                    std::ios::binary | std::ios::trunc);
                rejected << (version.empty() ? "unknown" : version);
                rejected.flush();
                if(rejected) return true;
                error = "Could not record the rejected yt-dlp version.";
                return false;
            }

            void RestoreForRetry() noexcept
            {
                std::error_code ignored;
                if(candidateActive_)
                {
                    std::filesystem::remove(next_, ignored);
                    ignored.clear();
                    std::filesystem::rename(active_, next_, ignored);
                    ignored.clear();
                    if(std::filesystem::is_regular_file(
                           active_.string() + ".version", ignored))
                    {
                        ignored.clear();
                        std::filesystem::remove(
                            next_.string() + ".version", ignored);
                        ignored.clear();
                        std::filesystem::rename(
                            active_.string() + ".version",
                            next_.string() + ".version",
                            ignored);
                    }
                    candidateActive_ = false;
                }
                if(originalMoved_)
                {
                    ignored.clear();
                    std::filesystem::remove(active_, ignored);
                    ignored.clear();
                    std::filesystem::rename(previous_, active_, ignored);
                    ignored.clear();
                    if(std::filesystem::is_regular_file(
                           previous_.string() + ".version", ignored))
                    {
                        ignored.clear();
                        std::filesystem::remove(
                            active_.string() + ".version", ignored);
                        ignored.clear();
                        std::filesystem::rename(
                            previous_.string() + ".version",
                            active_.string() + ".version",
                            ignored);
                    }
                    originalMoved_ = false;
                }
            }

            std::filesystem::path runtime_;
            std::filesystem::path next_;
            std::filesystem::path active_;
            std::filesystem::path previous_;
            std::string candidateVersion_;
            bool attempted_ = false;
            bool accepted_ = false;
            bool hadActive_ = false;
            bool originalMoved_ = false;
            bool candidateActive_ = false;
        };

        const char* StateName(DownloadState state)
        {
            switch(state)
            {
                case DownloadState::Preparing: return "preparing";
                case DownloadState::Probing: return "probing";
                case DownloadState::ProbeCompleted: return "probe_completed";
                case DownloadState::Downloading: return "downloading";
                case DownloadState::Completed: return "completed";
                case DownloadState::UpdateAvailable: return "update_available";
                case DownloadState::UpToDate: return "up_to_date";
                case DownloadState::Cancelled: return "cancelled";
                case DownloadState::Failed: return "failed";
                default: return "idle";
            }
        }

        DownloadState ParseState(const std::string& state)
        {
            if(state == "preparing") return DownloadState::Preparing;
            if(state == "probing") return DownloadState::Probing;
            if(state == "probe_completed") return DownloadState::ProbeCompleted;
            if(state == "downloading") return DownloadState::Downloading;
            if(state == "completed") return DownloadState::Completed;
            if(state == "update_available") return DownloadState::UpdateAvailable;
            if(state == "up_to_date") return DownloadState::UpToDate;
            if(state == "cancelled") return DownloadState::Cancelled;
            if(state == "failed") return DownloadState::Failed;
            return DownloadState::Idle;
        }

        std::string ReadString(const rapidjson::Value& object, const char* name)
        {
            if(!object.IsObject()) return {};
            const auto value = object.FindMember(name);
            return value != object.MemberEnd() && value->value.IsString()
                ? std::string(value->value.GetString(), value->value.GetStringLength())
                : std::string{};
        }

        double ReadNumber(const rapidjson::Value& object, const char* name)
        {
            if(!object.IsObject()) return 0.0;
            const auto value = object.FindMember(name);
            return value != object.MemberEnd() && value->value.IsNumber()
                ? value->value.GetDouble()
                : 0.0;
        }

        void AddString(
            rapidjson::Value& object,
            const char* name,
            const std::string& value,
            rapidjson::Document::AllocatorType& allocator)
        {
            object.AddMember(
                rapidjson::Value(name, allocator).Move(),
                rapidjson::Value(value.c_str(),
                                 static_cast<rapidjson::SizeType>(value.size()),
                                 allocator).Move(),
                allocator);
        }

        bool LoadGlobalLibrary(const std::filesystem::path& path, std::string& error)
        {
            if(!std::filesystem::is_regular_file(path))
            {
                error = "Missing runtime library: " + path.filename().string();
                return false;
            }
            if(!dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL))
            {
                error = "Could not load " + path.filename().string() + ": " + dlerror();
                return false;
            }
            return true;
        }

        bool CopyNativeLibrary(
            const std::filesystem::path& source,
            const std::filesystem::path& destination,
            std::string& error)
        {
            std::error_code fileError;
            if(!std::filesystem::is_regular_file(source, fileError))
            {
                error = "Missing runtime library: " + source.filename().string();
                return false;
            }

            std::filesystem::create_directories(destination.parent_path(), fileError);
            if(fileError)
            {
                error = "Could not create the private native runtime folder: " +
                        fileError.message();
                return false;
            }

            // Android deliberately refuses to map executable pages directly
            // from /sdcard. QMOD files have to arrive through shared storage,
            // so copy native dependencies into the app's private code cache
            // before CPython or dlopen attempts to load them.
            // Size-only comparisons can preserve a different same-sized .so
            // after an update. Compare the complete files before reusing the
            // executable private copy; this runs once during game startup.
            if(FilesEqual(source, destination))
                return true;

            fileError.clear();
            auto temporary = destination;
            temporary += ".next";
            std::filesystem::remove(temporary, fileError);
            fileError.clear();
            std::filesystem::copy_file(
                source,
                temporary,
                std::filesystem::copy_options::overwrite_existing,
                fileError);
            if(fileError)
            {
                error = "Could not stage " + source.filename().string() + ": " +
                        fileError.message();
                return false;
            }
            std::filesystem::permissions(
                temporary,
                std::filesystem::perms::owner_all |
                    std::filesystem::perms::group_read |
                    std::filesystem::perms::group_exec,
                std::filesystem::perm_options::replace,
                fileError);
            if(fileError)
            {
                error = "Could not make " + source.filename().string() +
                        " executable: " + fileError.message();
                return false;
            }
            std::filesystem::remove(destination, fileError);
            fileError.clear();
            std::filesystem::rename(temporary, destination, fileError);
            if(fileError)
            {
                error = "Could not activate " + source.filename().string() + ": " +
                        fileError.message();
                return false;
            }
            return true;
        }

        bool StageNativeRuntime(
            const std::filesystem::path& externalRuntime,
            const std::filesystem::path& modLibraries,
            std::string& error)
        {
            for(const char* name : {
                    "libcrypto_python.so",
                    "libssl_python.so",
                    "libsqlite3_python.so"})
            {
                if(!CopyNativeLibrary(
                    modLibraries / name,
                    InternalNativeRuntime / name,
                    error))
                    return false;
            }

            const auto sourceExtensions = externalRuntime / "lib-dynload";
            const auto privateExtensions = InternalNativeRuntime / "lib-dynload";
            std::unordered_set<std::string> expectedExtensions;
            std::ifstream manifestStream(
                externalRuntime / "runtime-manifest.json", std::ios::binary);
            const std::string manifestJson{
                std::istreambuf_iterator<char>(manifestStream), {}};
            rapidjson::Document manifest;
            manifest.Parse(manifestJson.data(), manifestJson.size());
            if(manifest.HasParseError() || !manifest.IsObject())
            {
                error = "The CPython runtime manifest is missing or invalid.";
                return false;
            }
            const auto extensionList = manifest.FindMember("nativeExtensions");
            if(extensionList == manifest.MemberEnd() ||
               !extensionList->value.IsArray() ||
               extensionList->value.Empty() ||
               extensionList->value.Size() > 256)
            {
                error = "The CPython runtime manifest has an invalid native-extension list.";
                return false;
            }
            for(const auto& entry : extensionList->value.GetArray())
            {
                if(!entry.IsString())
                {
                    error = "The CPython runtime manifest contains an invalid extension name.";
                    return false;
                }
                const std::string name{entry.GetString(), entry.GetStringLength()};
                const auto path = std::filesystem::path{name};
                if(name.empty() || path.filename() != path || path.extension() != ".so" ||
                   !expectedExtensions.emplace(name).second)
                {
                    error = "The CPython runtime manifest contains an unsafe or duplicate extension name.";
                    return false;
                }
                if(!CopyNativeLibrary(
                    sourceExtensions / name,
                    privateExtensions / name,
                    error))
                    return false;
            }

            // Both folders are mod-owned. Keep them exact mirrors of the
            // manifest so extensions removed by a later QMOD do not linger in
            // shared storage or remain importable from the executable cache.
            std::error_code iteratorError;
            std::filesystem::directory_iterator sourceEntries(
                sourceExtensions, iteratorError);
            if(iteratorError)
            {
                error = "Could not verify the staged CPython extension folder: " +
                        iteratorError.message();
                return false;
            }
            for(const auto& entry : sourceEntries)
            {
                if(entry.path().extension() == ".so" &&
                   !expectedExtensions.contains(entry.path().filename().string()))
                {
                    std::filesystem::remove(entry.path(), iteratorError);
                    if(iteratorError)
                    {
                        error = "Could not remove obsolete staged CPython extension " +
                                entry.path().filename().string() + ": " +
                                iteratorError.message();
                        return false;
                    }
                }
            }

            iteratorError.clear();
            std::filesystem::directory_iterator privateEntries(
                privateExtensions, iteratorError);
            if(iteratorError)
            {
                error = "Could not verify the private CPython extension folder: " +
                        iteratorError.message();
                return false;
            }
            for(const auto& entry : privateEntries)
            {
                if(entry.path().extension() == ".so" &&
                   !expectedExtensions.contains(entry.path().filename().string()))
                {
                    std::filesystem::remove(entry.path(), iteratorError);
                    if(iteratorError)
                    {
                        error = "Could not remove stale CPython extension " +
                                entry.path().filename().string() + ": " +
                                iteratorError.message();
                        return false;
                    }
                }
            }
            return true;
        }

        constexpr const char* DownloaderScript = R"PY(
import json, os, re, shutil, time, traceback, urllib.parse, urllib.request

job = json.loads(BIGSCREEN_JOB)
status_path = job['statusPath']
cancel_path = job['cancelPath']

def publish(state, message='', durable=True, **values):
    data = {'state': state, 'message': message}
    data.update(values)
    temporary = status_path + '.tmp'
    with open(temporary, 'w', encoding='utf-8') as stream:
        json.dump(data, stream, ensure_ascii=False)
        if durable:
            stream.flush()
            os.fsync(stream.fileno())
    os.replace(temporary, status_path)

last_progress_publish = 0.0

def cancelled():
    if os.path.exists(cancel_path):
        raise RuntimeError('BIGSCREEN_CANCELLED')

def progress(data):
    global last_progress_publish
    cancelled()
    if data.get('status') == 'downloading':
        now = time.monotonic()
        # yt-dlp can call progress hooks for every downloaded block. Publishing
        # at most eight times per second keeps the UI fluid without repeatedly
        # parsing JSON or forcing transient progress to flash storage.
        if now - last_progress_publish < 0.125:
            return
        last_progress_publish = now
        publish(
            'downloading',
            'Downloading video',
            durable=False,
            downloadedBytes=data.get('downloaded_bytes') or 0,
            totalBytes=data.get('total_bytes') or data.get('total_bytes_estimate') or 0,
            speed=data.get('speed') or 0,
            eta=data.get('eta') or 0)

def clean_error(value):
    text = re.sub(r'\x1b\[[0-9;]*m', '', str(value)).strip()
    return text[-700:]

def classify(value):
    text = clean_error(value)
    lower = text.lower()
    if 'bigscreen_cancelled' in lower:
        return 'cancelled', 'Download paused. Select Resume to continue it.'
    if ('sign in to confirm your age' in lower or
            'confirm your age' in lower or
            'age-restricted' in lower):
        return 'failed', 'YouTube requires a signed-in account for this age-restricted video; Big Screen cannot download logged-in videos.'
    if 'private video' in lower:
        return 'failed', 'This YouTube video is private and cannot be downloaded.'
    if ('members-only' in lower or 'members only' in lower or
            'join this channel' in lower):
        return 'failed', 'This is a members-only YouTube video and requires a signed-in account with channel access.'
    if ('sign in to confirm you' in lower or
            'login required' in lower or
            'log in' in lower and 'required' in lower or
            'authentication' in lower and 'required' in lower or
            'cookies-from-browser' in lower):
        return 'failed', 'YouTube requires a signed-in account to view this video; Big Screen does not use or store YouTube login credentials.'
    if 'premium' in lower and ('required' in lower or 'only' in lower):
        return 'failed', 'This video requires a YouTube Premium account and cannot be downloaded without signing in.'
    if ('removed by the uploader' in lower or
            'video has been removed' in lower or
            'video unavailable' in lower):
        return 'failed', 'This YouTube video is unavailable or has been removed.'
    if 'copyright' in lower and ('blocked' in lower or 'claim' in lower):
        return 'failed', 'YouTube has blocked this video because of a copyright restriction.'
    if 'not available in your country' in lower or 'geo' in lower and 'restricted' in lower:
        return 'failed', 'This video is not available in your region.'
    if 'requested format is not available' in lower or 'no video formats' in lower:
        return 'failed', 'This video has no H.264 MP4 stream at 1080p or lower.'
    if 'no space left' in lower:
        return 'failed', 'The Quest ran out of free storage while downloading.'
    if 'http error 400' in lower or ('400' in lower and 'bad request' in lower):
        return 'failed', 'YouTube returned HTTP 400 (Bad Request). It rejected the video request, usually because the link is malformed or the downloader needs an update.'
    if 'http error 401' in lower or ('401' in lower and 'unauthorized' in lower):
        return 'failed', 'YouTube returned HTTP 401 (Unauthorized). This video requires account authentication, which Big Screen does not use or store.'
    if 'http error 403' in lower or ('403' in lower and 'forbidden' in lower):
        return 'failed', 'YouTube returned HTTP 403 (Forbidden). YouTube understood the request but refused access to this video stream. The video may be restricted, or yt-dlp may need an update.'
    if 'http error 404' in lower or ('404' in lower and 'not found' in lower):
        return 'failed', 'YouTube returned HTTP 404 (Not Found). The video address no longer points to an available video.'
    if 'http error 410' in lower or ('410' in lower and 'gone' in lower):
        return 'failed', 'YouTube returned HTTP 410 (Gone). The video was removed and is no longer available from this address.'
    if 'http error 429' in lower or 'too many requests' in lower:
        return 'failed', 'YouTube returned HTTP 429 (Too Many Requests). It is temporarily rate-limiting this headset; wait and try again later.'
    if re.search(r'http error 5\d\d', lower):
        return 'failed', 'YouTube returned a 5xx server error. YouTube could not complete the request; this is normally temporary, so try again later.'
    if 'certificate verify failed' in lower:
        return 'failed', 'Secure connection failed because the certificate could not be verified.'
    if 'unable to download' in lower or 'network is unreachable' in lower or 'timed out' in lower:
        return 'failed', 'Network download failed: ' + text
    return 'failed', 'YouTube download failed: ' + text

def diagnostic_code(value):
    """Return a stable support code without exposing yt-dlp internals in UI."""
    lower = clean_error(value).lower()
    if 'bigscreen_cancelled' in lower:
        return 'BS-DL-CANCELLED'
    if 'http error 400' in lower:
        return 'BS-DL-HTTP-400'
    if 'http error 401' in lower:
        return 'BS-DL-HTTP-401'
    if 'http error 403' in lower:
        return 'BS-DL-HTTP-403'
    if 'http error 404' in lower:
        return 'BS-DL-HTTP-404'
    if 'http error 410' in lower:
        return 'BS-DL-HTTP-410'
    if 'http error 429' in lower:
        return 'BS-DL-HTTP-429'
    if re.search(r'http error 5\d\d', lower):
        return 'BS-DL-HTTP-5XX'
    if 'certificate verify failed' in lower:
        return 'BS-DL-TLS-001'
    if 'no space left' in lower or 'not enough free quest storage' in lower:
        return 'BS-DL-STORAGE-001'
    if 'requested format is not available' in lower or 'no video formats' in lower:
        return 'BS-DL-FORMAT-001'
    if ('private video' in lower or 'members-only' in lower or
            'members only' in lower or 'age-restricted' in lower or
            'sign in' in lower or 'login required' in lower):
        return 'BS-DL-ACCESS-001'
    return 'BS-DL-FAILED-001'

def stream_summary(candidate):
    """Describe a stream for logs without persisting its signed Google URL."""
    client = 'unknown'
    try:
        query = urllib.parse.parse_qs(
            urllib.parse.urlparse(str(candidate.get('url') or '')).query)
        client = str((query.get('c') or ['unknown'])[0])
    except BaseException:
        pass
    return 'format=%s, dimensions=%sx%s, protocol=%s, client=%s' % (
        candidate.get('format_id') or 'unknown',
        candidate.get('width') or 0,
        candidate.get('height') or 0,
        candidate.get('protocol') or 'unknown',
        client)

try:
    publish('preparing', 'Checking video information')
    cancelled()
    # Importing this module registers Big Screen's in-process JavaScript
    # challenge provider before yt-dlp creates the YouTube extractor.
    import bigscreen_jsc_provider
    import yt_dlp
    # Permit either orientation up to Full HD. A portrait 1080x1920 stream has
    # height 1920 and was incorrectly rejected by the old height-only rule.
    selector = 'bestvideo[ext=mp4][vcodec^=avc1][width<=1920][height<=1920]'
    common = {
        'quiet': True,
        'no_warnings': True,
        'noplaylist': True,
        'format': selector,
        'continuedl': True,
        'nopart': False,
        'progress_hooks': [progress],
        'overwrites': True,
        # Cancellation is observed by progress hooks. Bound individual network
        # waits and retries so a disconnected Quest can still leave Beat Saber
        # or shut down without waiting indefinitely for a Python worker.
        'socket_timeout': 15,
        'retries': 3,
        'fragment_retries': 3,
        'extractor_retries': 3,
        # Pin the Android VR client so Quest downloads do not silently switch
        # to a web client whose Google Video Server streams can require a PO
        # token. QuickJS/EJS handles JavaScript challenges; PO tokens are a
        # separate mechanism and are not needed by this client today.
        'extractor_args': {'youtube': {'player_client': ['android_vr']}},
    }
    with yt_dlp.YoutubeDL(dict(common, skip_download=True)) as probe:
        info = probe.extract_info(job['sourceUrl'], download=False)
    age_limit = int(info.get('age_limit') or 0)
    if age_limit >= 18 and not job.get('explicitContentAllowed', False):
        raise PermissionError('Big Screen blocked this age-restricted video because explicit content is disabled in Beat Saber parental controls.')
    formats = []
    for candidate in info.get('formats') or []:
        width = int(candidate.get('width') or 0)
        height = int(candidate.get('height') or 0)
        if (candidate.get('ext') == 'mp4'
                and str(candidate.get('vcodec') or '').startswith('avc1')
                and width > 0 and height > 0
                and max(width, height) <= 1920
                and min(width, height) <= 1080
                and candidate.get('acodec') == 'none'):
            formats.append(candidate)
    if not formats:
        raise RuntimeError('Requested format is not available: no H.264 MP4 video-only stream at 1080p or lower')
    chosen = max(formats, key=lambda f: (
        int(f.get('width') or 0) * int(f.get('height') or 0),
        max(int(f.get('width') or 0), int(f.get('height') or 0)),
        float(f.get('tbr') or 0)))
    expected = int(chosen.get('filesize') or chosen.get('filesize_approx') or 0)
    free = shutil.disk_usage(os.path.dirname(job['finalPath'])).free
    required = expected + int(job['reserveBytes']) if expected else int(job['unknownRequiredBytes'])
    if free < required:
        raise OSError('Not enough free Quest storage. Need at least %.1f MB free; %.1f MB is available.' % (required / 1048576, free / 1048576))
    options = dict(common, outtmpl=job['finalPath'], format=chosen['format_id'])
    part_path = job['finalPath'] + '.part'
    retry_detail = ''
    try:
        with yt_dlp.YoutubeDL(options) as downloader:
            result = downloader.extract_info(job['sourceUrl'], download=True)
    except BaseException as first_error:
        # A signed Google video URL can expire or reject a resumed Range
        # request even though metadata probing succeeded. If yt-dlp wrote a
        # partial file and then received 403, discard only that temporary file,
        # obtain a fresh stream URL, and retry once from byte zero. The final
        # library video is never removed by this recovery path.
        first_text = clean_error(first_error)
        partial_bytes = 0
        try:
            partial_bytes = os.path.getsize(part_path)
        except OSError:
            pass
        if 'http error 403' not in first_text.lower() or partial_bytes <= 0:
            raise
        cancelled()
        os.remove(part_path)
        retry_detail = (
            'Initial 403 occurred after %d partial bytes; Big Screen retried '
            'once from byte zero with a freshly extracted stream. ' %
            partial_bytes)
        clean_options = dict(options, continuedl=False)
        try:
            with yt_dlp.YoutubeDL(clean_options) as downloader:
                result = downloader.extract_info(job['sourceUrl'], download=True)
        except BaseException as retry_error:
            raise RuntimeError(
                retry_detail + 'Initial detail: ' + first_text +
                ' Fresh retry detail: ' + clean_error(retry_error)) from retry_error
    cancelled()
    size = os.path.getsize(job['finalPath'])

    # Keep the video's own YouTube artwork beside Big Screen's durable video
    # library. Thumbnail failure must not discard a successfully downloaded
    # video, but an older thumbnail must never be left behind after replacing
    # a video with a different URL.
    thumbnail_path = job['thumbnailPath']
    published_thumbnail = ''
    try:
        thumbnail_url = str(result.get('thumbnail') or info.get('thumbnail') or '')
        if thumbnail_url:
            request = urllib.request.Request(
                thumbnail_url,
                headers={'User-Agent': 'Big-Screen-Beat-Saber'})
            with urllib.request.urlopen(request, timeout=20) as response:
                image = response.read(4 * 1024 * 1024 + 1)
            if len(image) > 4 * 1024 * 1024:
                raise RuntimeError('The YouTube thumbnail was unexpectedly large.')
            temporary_thumbnail = thumbnail_path + '.tmp'
            with open(temporary_thumbnail, 'wb') as stream:
                stream.write(image)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary_thumbnail, thumbnail_path)
            published_thumbnail = thumbnail_path
        elif os.path.exists(thumbnail_path):
            os.remove(thumbnail_path)
    except BaseException:
        if os.path.exists(thumbnail_path):
            os.remove(thumbnail_path)

    publish(
        'completed',
        'Video downloaded',
        title=result.get('title') or info.get('title') or '',
        duration=result.get('duration') or info.get('duration') or 0,
        width=chosen.get('width') or 0,
        height=chosen.get('height') or 0,
        codec=chosen.get('vcodec') or 'h264',
        thumbnailPath=published_thumbnail,
        bytes=size,
        downloadedBytes=size,
        totalBytes=size)
except PermissionError as error:
    code = diagnostic_code(error)
    publish(
        'failed', clean_error(error) + ' (' + code + ')',
        errorCode=code,
        diagnostic=clean_error(error))
except BaseException as error:
    state, message = classify(error)
    code = diagnostic_code(error)
    detail = clean_error(error)
    if 'chosen' in globals():
        detail += ' | ' + stream_summary(chosen)
    publish(
        state,
        message if state == 'cancelled' else message + ' (' + code + ')',
        errorCode=code,
        diagnostic=detail)
)PY";

        constexpr const char* ProbeScript = R"PY(
import json, os, re, urllib.request

job = json.loads(BIGSCREEN_JOB)
status_path = job['statusPath']

def publish(state, message='', **values):
    data = {'state': state, 'message': message}
    data.update(values)
    temporary = status_path + '.tmp'
    with open(temporary, 'w', encoding='utf-8') as stream:
        json.dump(data, stream, ensure_ascii=False)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, status_path)

def clean_error(value):
    return re.sub(r'\x1b\[[0-9;]*m', '', str(value)).strip()[-700:]

def classify(value):
    text = clean_error(value)
    lower = text.lower()
    if 'private video' in lower:
        return 'This YouTube video is private and cannot be downloaded.'
    if ('sign in to confirm your age' in lower or 'confirm your age' in lower or
            'age-restricted' in lower):
        return 'YouTube requires a signed-in account for this age-restricted video; Big Screen cannot download logged-in videos.'
    if ('members-only' in lower or 'members only' in lower or
            'join this channel' in lower):
        return 'This is a members-only YouTube video and requires a signed-in account with channel access.'
    if ('sign in to confirm you' in lower or 'login required' in lower or
            ('log in' in lower and 'required' in lower) or
            ('authentication' in lower and 'required' in lower) or
            'cookies-from-browser' in lower):
        return 'YouTube requires a signed-in account to view this video; Big Screen does not use or store YouTube login credentials.'
    if 'not available in your country' in lower or ('geo' in lower and 'restricted' in lower):
        return 'This video is not available in your region.'
    if ('removed by the uploader' in lower or 'video has been removed' in lower or
            'video unavailable' in lower):
        return 'This YouTube video is unavailable or has been removed.'
    if 'http error 400' in lower or ('400' in lower and 'bad request' in lower):
        return 'YouTube returned HTTP 400 (Bad Request). It rejected the video request, usually because the link is malformed or the downloader needs an update.'
    if 'http error 401' in lower or ('401' in lower and 'unauthorized' in lower):
        return 'YouTube returned HTTP 401 (Unauthorized). This video requires account authentication, which Big Screen does not use or store.'
    if 'http error 403' in lower or ('403' in lower and 'forbidden' in lower):
        return 'YouTube returned HTTP 403 (Forbidden). YouTube understood the request but refused access to this video stream. The video may be restricted, or yt-dlp may need an update.'
    if 'http error 404' in lower or ('404' in lower and 'not found' in lower):
        return 'YouTube returned HTTP 404 (Not Found). The video address no longer points to an available video.'
    if 'http error 410' in lower or ('410' in lower and 'gone' in lower):
        return 'YouTube returned HTTP 410 (Gone). The video was removed and is no longer available from this address.'
    if 'http error 429' in lower or 'too many requests' in lower:
        return 'YouTube returned HTTP 429 (Too Many Requests). It is temporarily rate-limiting this headset; wait and try again later.'
    if re.search(r'http error 5\d\d', lower):
        return 'YouTube returned a 5xx server error. YouTube could not complete the request; this is normally temporary, so try again later.'
    if 'certificate verify failed' in lower:
        return 'Secure connection failed because the YouTube certificate could not be verified.'
    if 'network is unreachable' in lower or 'timed out' in lower:
        return 'The Quest could not reach YouTube: ' + text
    return 'YouTube could not recognize this URL: ' + text

try:
    publish('probing', 'Checking YouTube URL')
    import bigscreen_jsc_provider
    import yt_dlp
    with yt_dlp.YoutubeDL({
            'quiet': True,
            'no_warnings': True,
            'noplaylist': True,
            'skip_download': True}) as probe:
        info = probe.extract_info(job['sourceUrl'], download=False)
    video_id = str(info.get('id') or '')
    title = str(info.get('title') or 'YouTube video')
    if not video_id:
        raise RuntimeError('YouTube did not return a video identifier.')

    # The standard YouTube thumbnail endpoint is consistently JPEG, which
    # Unity can decode directly without bundling another image codec.
    thumbnail_url = 'https://i.ytimg.com/vi/' + video_id + '/hqdefault.jpg'
    request = urllib.request.Request(
        thumbnail_url,
        headers={'User-Agent': 'Big-Screen-Beat-Saber'})
    with urllib.request.urlopen(request, timeout=20) as response:
        image = response.read(4 * 1024 * 1024 + 1)
    if len(image) > 4 * 1024 * 1024:
        raise RuntimeError('The YouTube thumbnail was unexpectedly large.')
    temporary = job['thumbnailPath'] + '.tmp'
    with open(temporary, 'wb') as stream:
        stream.write(image)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, job['thumbnailPath'])
    publish(
        'probe_completed',
        'Recognized: ' + title,
        title=title,
        thumbnailPath=job['thumbnailPath'])
except BaseException as error:
    publish('failed', classify(error))
)PY";

        constexpr const char* UpdaterScript = R"PY(
import hashlib, json, os, urllib.request, zipfile
job = json.loads(BIGSCREEN_JOB)
def publish(state, message='', **extra):
    value = {'state': state, 'message': message}; value.update(extra)
    temporary = job['statusPath'] + '.tmp'
    with open(temporary, 'w', encoding='utf-8') as stream:
        json.dump(value, stream); stream.flush(); os.fsync(stream.fileno())
    os.replace(temporary, job['statusPath'])
try:
    publish('preparing', 'Checking yt-dlp releases')
    repository = 'yt-dlp/yt-dlp-nightly-builds' if job['nightly'] else 'yt-dlp/yt-dlp'
    request = urllib.request.Request(
        'https://api.github.com/repos/' + repository + '/releases/latest',
        headers={'User-Agent': 'Big-Screen-Beat-Saber'})
    with urllib.request.urlopen(request, timeout=30) as response:
        release = json.load(response)
    version = str(release.get('tag_name') or '')
    current = job['currentVersion']
    rejected = job.get('rejectedVersion', '')
    if version and version == rejected:
        publish('up_to_date', 'yt-dlp ' + version + ' was rejected on this headset. Big Screen will wait for a newer release.', version=version)
    elif version == current and not job['install']:
        publish('up_to_date', 'yt-dlp ' + current + ' is current', version=version)
    elif not job['install']:
        publish('update_available', 'yt-dlp ' + version + ' is available. Select Install Update to download it.', version=version)
    else:
        assets = {asset['name']: asset['browser_download_url'] for asset in release.get('assets', [])}
        package_url = assets.get('yt-dlp')
        sums_url = assets.get('SHA2-256SUMS')
        if not package_url or not sums_url:
            raise RuntimeError('The selected release does not contain an official SHA-256 checksum list.')
        with urllib.request.urlopen(urllib.request.Request(sums_url, headers={'User-Agent':'Big-Screen-Beat-Saber'}), timeout=30) as response:
            sums = response.read().decode('utf-8')
        expected = next((line.split()[0] for line in sums.splitlines() if line.rstrip().endswith('yt-dlp')), '')
        if not expected:
            raise RuntimeError('yt-dlp checksum list did not include the Android-compatible Python package.')
        temporary = job['nextPath'] + '.part'
        digest = hashlib.sha256()
        maximum_package_bytes = 32 * 1024 * 1024
        with urllib.request.urlopen(urllib.request.Request(package_url, headers={'User-Agent':'Big-Screen-Beat-Saber'}), timeout=60) as response, open(temporary, 'wb') as output:
            declared_size = int(response.headers.get('Content-Length') or 0)
            if declared_size > maximum_package_bytes:
                raise RuntimeError('The yt-dlp update is larger than Big Screen\'s 32 MB safety limit.')
            downloaded_size = 0
            while True:
                block = response.read(262144)
                if not block: break
                downloaded_size += len(block)
                if downloaded_size > maximum_package_bytes:
                    raise RuntimeError('The yt-dlp update exceeded Big Screen\'s 32 MB safety limit.')
                output.write(block); digest.update(block)
        if digest.hexdigest().lower() != expected.lower():
            os.remove(temporary)
            raise RuntimeError('Downloaded yt-dlp SHA-256 did not match the official release checksum.')
        with zipfile.ZipFile(temporary) as package:
            required_entries = {
                'yt_dlp/__init__.py',
                'yt_dlp_ejs/__init__.py',
                'yt_dlp_ejs/yt/solver/__init__.py',
                'yt_dlp_ejs/yt/solver/core.min.js',
                'yt_dlp_ejs/yt/solver/lib.min.js',
            }
            if package.testzip() is not None or not required_entries.issubset(package.namelist()):
                raise RuntimeError('The downloaded yt-dlp package failed its compatibility self-test.')
        os.replace(temporary, job['nextPath'])
        with open(job['nextPath'] + '.version', 'w', encoding='utf-8') as version_file:
            version_file.write(version)
        publish('completed', 'yt-dlp ' + version + ' verified. Restart Beat Saber to activate it.', version=version)
except BaseException as error:
    temporary_path = locals().get('temporary', '')
    if temporary_path and os.path.exists(temporary_path):
        try:
            os.remove(temporary_path)
        except OSError:
            pass
    publish('failed', 'yt-dlp update failed: ' + str(error)[-700:])
)PY";

        // Video-library rows use the video's YouTube artwork, never Beat
        // Saber's album art. This small worker accepts only recognizable
        // YouTube URLs, derives the stable video id locally, and downloads the
        // standard JPEG without running a full yt-dlp metadata extraction for
        // every visible row.
        constexpr const char* ThumbnailScript = R"PY(
import json, os, re, urllib.parse, urllib.request

job = json.loads(BIGSCREEN_JOB)
parsed = urllib.parse.urlparse(job['sourceUrl'])
host = (parsed.hostname or '').lower()
video_id = ''
if host in ('youtu.be', 'www.youtu.be'):
    video_id = parsed.path.strip('/').split('/')[0]
elif host == 'youtube.com' or host == 'www.youtube.com' or host.endswith('.youtube.com'):
    video_id = (urllib.parse.parse_qs(parsed.query).get('v') or [''])[0]
    if not video_id:
        parts = [part for part in parsed.path.split('/') if part]
        if len(parts) >= 2 and parts[0] in ('embed', 'shorts', 'live'):
            video_id = parts[1]
if not re.fullmatch(r'[A-Za-z0-9_-]{11}', video_id):
    raise RuntimeError('The configured URL does not contain a YouTube video id.')

request = urllib.request.Request(
    'https://i.ytimg.com/vi/' + video_id + '/hqdefault.jpg',
    headers={'User-Agent': 'Big-Screen-Beat-Saber'})
with urllib.request.urlopen(request, timeout=20) as response:
    image = response.read(4 * 1024 * 1024 + 1)
if len(image) > 4 * 1024 * 1024:
    raise RuntimeError('The YouTube thumbnail was unexpectedly large.')
temporary = job['destination'] + '.tmp'
with open(temporary, 'wb') as stream:
    stream.write(image)
    stream.flush()
    os.fsync(stream.fileno())
os.replace(temporary, job['destination'])
)PY";
    }

    DownloadManager& DownloadManager::Instance()
    {
        static DownloadManager manager;
        return manager;
    }

    DownloadManager::~DownloadManager()
    {
        Cancel();
        if(worker_.joinable()) worker_.join();
        {
            std::scoped_lock lock(thumbnailMutex_);
            stopThumbnailWorker_ = true;
            thumbnailQueue_.clear();
        }
        thumbnailWake_.notify_all();
        if(thumbnailWorker_.joinable()) thumbnailWorker_.join();
    }

    bool DownloadManager::Initialize(std::string& error)
    {
        if(initialized_) return true;
        const auto fail = [this, &error](
            const char* code,
            std::string detail) -> bool
        {
            initializationErrorCode_ = code;
            error = initializationErrorCode_ + ": " + std::move(detail);
            return false;
        };
        const auto runtime = VideoLibrary::Instance().RuntimePath();
        const auto active = runtime / "yt-dlp-active";
        const auto modLibraries = std::filesystem::path(
            "/sdcard/ModData/com.beatgames.beatsaber/Modloader/libs");
        if(!StageNativeRuntime(runtime, modLibraries, error))
            return fail("BS-DL-INIT-101", std::move(error));
        if(!LoadGlobalLibrary(InternalNativeRuntime / "libcrypto_python.so", error) ||
           !LoadGlobalLibrary(InternalNativeRuntime / "libssl_python.so", error) ||
           !LoadGlobalLibrary(InternalNativeRuntime / "libsqlite3_python.so", error))
            return fail("BS-DL-INIT-102", std::move(error));

        // Validate and publish the certificate path before CPython starts. If
        // this fails, Initialize() can safely be retried because no interpreter
        // has been created and DownloaderActivation restores any candidate.
        const auto certificateBundle = runtime / "certifi" / "cacert.pem";
        std::ifstream certificateStream(certificateBundle, std::ios::binary);
        std::string certificateHeader(4096, '\0');
        certificateStream.read(certificateHeader.data(), certificateHeader.size());
        certificateHeader.resize(static_cast<std::size_t>(certificateStream.gcount()));
        if(certificateHeader.find("-----BEGIN CERTIFICATE-----") == std::string::npos)
        {
            return fail(
                "BS-DL-INIT-103",
                "The packaged certificate authority bundle is missing or invalid.");
        }
        const auto bundlePath = certificateBundle.string();
        if(setenv("SSL_CERT_FILE", bundlePath.c_str(), 1) != 0 ||
           setenv("REQUESTS_CA_BUNDLE", bundlePath.c_str(), 1) != 0 ||
           setenv("CURL_CA_BUNDLE", bundlePath.c_str(), 1) != 0)
        {
            return fail(
                "BS-DL-INIT-104",
                "Could not configure the embedded certificate authority bundle.");
        }

        // Promote only after every fallible native/certificate prerequisite
        // has passed. From this point until Accept(), DownloaderActivation
        // owns restoration if bridge registration, CPython initialization, or
        // validation fails.
        DownloaderActivation activation{runtime};
        if(!activation.Promote(error))
            return fail("BS-DL-INIT-105", std::move(error));
        const auto promotedCandidate = activation.Promoted();
        const auto candidateVersion = activation.CandidateVersion();
        std::ifstream activeVersion(active.string() + ".version");
        if(activeVersion) std::getline(activeVersion, currentUpdateVersion_);

        // Register the compiled bridge before CPython starts. Keeping this as
        // a built-in module avoids Android's prohibition on executing a qjs
        // program from writable app storage and avoids another staged .so.
        if(PyImport_AppendInittab(
               "bigscreen_quickjs", PyInit_bigscreen_quickjs) == -1)
        {
            return fail(
                "BS-DL-INIT-106",
                "Could not register the in-process QuickJS-NG bridge.");
        }

        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);
        config.module_search_paths_set = 1;
        const std::array paths{
            runtime / "python314.zip",
            InternalNativeRuntime / "lib-dynload",
            runtime,
            runtime / "certifi.whl",
            runtime / "yt-dlp-active",
            runtime / "yt-dlp-shipped"
        };
        for(const auto& path : paths)
        {
            const auto wide = Py_DecodeLocale(path.c_str(), nullptr);
            if(!wide)
            {
                PyConfig_Clear(&config);
                return fail(
                    "BS-DL-INIT-107",
                    "Could not encode a Python runtime path.");
            }
            const auto status = PyWideStringList_Append(&config.module_search_paths, wide);
            PyMem_RawFree(wide);
            if(PyStatus_Exception(status))
            {
                const std::string detail = status.err_msg
                    ? status.err_msg
                    : "Could not configure the Python runtime path.";
                PyConfig_Clear(&config);
                return fail("BS-DL-INIT-107", detail);
            }
        }
        auto status = Py_InitializeFromConfig(&config);
        PyConfig_Clear(&config);
        if(PyStatus_Exception(status))
        {
            return fail(
                "BS-DL-INIT-108",
                status.err_msg ? status.err_msg : "Python initialization failed.");
        }

        // A checksum proves that the archive matches the official release; it
        // does not prove that this CPython build can import that release. Test
        // the exact active module and parse both shipped EJS solver bundles
        // before accepting it as authoritative. Use PyRun_String rather than
        // PyRun_SimpleString so the raised exception remains available for the
        // persistent diagnostic log instead of being printed and discarded.
        const auto smokeTest = []() -> std::string
        {
            constexpr const char* script =
                "import importlib, json, sys\n"
                "importlib.invalidate_caches()\n"
                "[sys.modules.pop(k, None) for k in list(sys.modules) if k == 'bigscreen_jsc_provider' or k == 'yt_dlp' or k.startswith('yt_dlp.') or k == 'yt_dlp_ejs' or k.startswith('yt_dlp_ejs.')]\n"
                "import bigscreen_quickjs\n"
                "quickjs_result = json.loads(bigscreen_quickjs.execute('console.log(JSON.stringify({value: 6 * 7}))'))\n"
                "if quickjs_result.get('value') != 42: raise RuntimeError('The QuickJS bridge returned an invalid result')\n"
                "import bigscreen_jsc_provider\n"
                "import yt_dlp_ejs\n"
                "if not isinstance(yt_dlp_ejs.version, str) or not yt_dlp_ejs.version: raise RuntimeError('yt-dlp-ejs has no version')\n"
                "from yt_dlp_ejs.yt import solver as ejs_solver\n"
                "ejs_library, ejs_core = ejs_solver.lib(), ejs_solver.core()\n"
                "if not ejs_library or not ejs_core: raise RuntimeError('The yt-dlp-ejs solver bundles are empty')\n"
                // EJS's library bundle intentionally exposes a single `lib`
                // object. The core solver expects its meriyah/astring members
                // on globalThis, exactly as EJSBaseJCP._construct_stdin does.
                // Evaluating library + core without this assignment caused a
                // false startup failure even though every file was present.
                "ejs_result = json.loads(bigscreen_quickjs.execute(ejs_library + '\\nObject.assign(globalThis, lib);\\n' + ejs_core + '\\nconsole.log(JSON.stringify({value: 6 * 7}));'))\n"
                "if ejs_result.get('value') != 42: raise RuntimeError('The yt-dlp-ejs bundles did not execute correctly')\n"
                "from yt_dlp.extractor.youtube.jsc._registry import _jsc_providers\n"
                "if 'BigScreenQuickJS' not in _jsc_providers.value: raise RuntimeError('The Big Screen JavaScript provider was not registered')\n"
                "from yt_dlp import YoutubeDL\n"
                "if not callable(YoutubeDL): raise RuntimeError('yt-dlp did not expose YoutubeDL')\n";
            PythonObject globals{PyDict_New()};
            if(!globals ||
               PyDict_SetItemString(
                   globals.get(), "__builtins__", PyEval_GetBuiltins()) < 0)
                return TakePythonExceptionText();
            PythonObject result{PyRun_StringFlags(
                script,
                Py_file_input,
                globals.get(),
                globals.get(),
                nullptr)};
            return result ? std::string{} : TakePythonExceptionText();
        };
        auto smokeTestError = smokeTest();
        if(!smokeTestError.empty())
        {
            if(promotedCandidate)
            {
                // Restore the one previous working package, or fall back to
                // the immutable shipped baseline when no previous update
                // exists. Normal video/network failures never enter this path.
                std::string rollbackError;
                if(!activation.Reject(rollbackError))
                {
                    initializationErrorCode_ = "BS-DL-INIT-112";
                    error = initializationErrorCode_ +
                        ": The new yt-dlp package failed its startup test, and Big Screen could not restore the previous package: " +
                        rollbackError + " Python detail:\n" + smokeTestError;
                    PyEval_SaveThread();
                    return false;
                }
                currentUpdateVersion_ = "2026.07.04";
                std::ifstream restoredVersion(active.string() + ".version");
                if(restoredVersion)
                    std::getline(restoredVersion, currentUpdateVersion_);
                smokeTestError = smokeTest();
                if(!smokeTestError.empty())
                {
                    initializationErrorCode_ = "BS-DL-INIT-110";
                    error = initializationErrorCode_ +
                        ": Neither the previous nor shipped yt-dlp package could be imported. Python detail:\n" +
                        smokeTestError;
                    PyEval_SaveThread();
                    return false;
                }
                std::scoped_lock lock(mutex_);
                updateNotice_ =
                    "The new yt-dlp package could not load on this Quest. Big Screen automatically restored the previous working downloader and marked the update as rejected.";
                PaperLogger.warn(
                    "Rejected yt-dlp candidate '{}' and restored the previous package",
                    candidateVersion);
            }
            else
            {
                // A package activated by an older Big Screen build can still
                // be present without a pending `next` transaction. Reject that
                // stranded update once and retry the immutable shipped copy.
                if(std::filesystem::is_regular_file(active))
                {
                    auto rejectedVersion = currentUpdateVersion_;
                    std::error_code fileError;
                    std::filesystem::remove(active, fileError);
                    if(!fileError)
                        std::filesystem::remove(active.string() + ".version", fileError);
                    if(fileError)
                    {
                        initializationErrorCode_ = "BS-DL-INIT-113";
                        error = initializationErrorCode_ +
                            ": The active yt-dlp update failed its startup test and could not be removed: " +
                            fileError.message() + " Python detail:\n" +
                            smokeTestError;
                        PyEval_SaveThread();
                        return false;
                    }
                    std::ofstream rejected(
                        runtime / "yt-dlp-rejected.version",
                        std::ios::binary | std::ios::trunc);
                    rejected << (rejectedVersion.empty() ? "unknown" : rejectedVersion);
                    currentUpdateVersion_ = "2026.07.04";
                    smokeTestError = smokeTest();
                    if(smokeTestError.empty())
                    {
                        std::scoped_lock lock(mutex_);
                        updateNotice_ =
                            "The installed yt-dlp update could not load on this Quest. Big Screen removed it and restored the built-in downloader.";
                    }
                    else
                    {
                        initializationErrorCode_ = "BS-DL-INIT-111";
                        error = initializationErrorCode_ +
                            ": Neither the installed nor shipped yt-dlp package could be imported. Python detail:\n" +
                            smokeTestError;
                        PyEval_SaveThread();
                        return false;
                    }
                }
                else
                {
                    initializationErrorCode_ = "BS-DL-INIT-109";
                    error = initializationErrorCode_ +
                        ": The shipped yt-dlp package failed its startup compatibility test. Python detail:\n" +
                        smokeTestError;
                    PyEval_SaveThread();
                    return false;
                }
            }
        }
        else if(promotedCandidate)
        {
            // A candidate is not trusted merely because it downloaded and
            // matched its checksum. Clear the rejection marker only after the
            // newly activated package imports successfully on this headset.
            activation.Accept();
        }
        PyEval_SaveThread();
        initialized_ = true;
        PaperLogger.info(
            "Embedded CPython downloader initialized with QuickJS-NG {} and CA bundle '{}'",
            QuickJsVersion,
            certificateBundle.string());
        return true;
    }

    std::string DownloadManager::UnavailableMessage() const
    {
        return "The embedded downloader runtime is not available (" +
            initializationErrorCode_ + "). See Big Screen's error log for details.";
    }

    std::optional<std::string> DownloadManager::TakeUpdateNotice()
    {
        std::scoped_lock lock(mutex_);
        auto result = updateNotice_;
        updateNotice_.reset();
        return result;
    }

    bool DownloadManager::Start(DownloadRequest request, std::string& error)
    {
        if(!initialized_)
        {
            error = UnavailableMessage();
            return false;
        }
        {
            std::scoped_lock lock(mutex_);
            RefreshSnapshotFromDiskLocked();
            if(snapshot_.Active())
            {
                error = "Another video is already downloading.";
                return false;
            }
        }
        if(request.levelId.empty() || request.sourceUrl.empty())
        {
            error = "A song and YouTube URL are required.";
            return false;
        }
        if(!CoreLogic::IsSupportedYouTubeUrl(request.sourceUrl))
        {
            error = "Only HTTPS YouTube and youtu.be video addresses are supported.";
            return false;
        }
        // Join only after releasing the state mutex. A worker can have written
        // its terminal status while still waiting to commit library metadata.
        if(worker_.joinable()) worker_.join();
        std::scoped_lock lock(mutex_);
        auto& library = VideoLibrary::Instance();
        const auto finalPath = library.AllocateVideoPath(request.levelId, request.origin);
        jobPath_ = library.RuntimePath() / "download-job.json";
        statusPath_ = library.RuntimePath() / "download-status.json";
        cancelPath_ = library.RuntimePath() / "download.cancel";
        std::filesystem::remove(cancelPath_);
        std::filesystem::remove(statusPath_);
        snapshot_ = {};
        snapshot_.state = DownloadState::Preparing;
        snapshot_.levelId = request.levelId;
        snapshot_.message = "Checking video information";
        PaperLogger.info(
            "Starting video download for '{}' ({})",
            request.songName,
            request.levelId);
        worker_ = std::thread([this, request = std::move(request), finalPath]() mutable {
            Run(std::move(request), finalPath);
        });
        return true;
    }

    bool DownloadManager::StartProbe(
        std::string levelId,
        std::string sourceUrl,
        std::string& error)
    {
        if(!initialized_)
        {
            error = UnavailableMessage();
            return false;
        }
        {
            std::scoped_lock lock(mutex_);
            RefreshSnapshotFromDiskLocked();
            if(snapshot_.Active())
            {
                error = "Another downloader task is already running.";
                return false;
            }
        }
        if(levelId.empty() || sourceUrl.empty())
        {
            error = "A song and YouTube URL are required.";
            return false;
        }
        if(!CoreLogic::IsSupportedYouTubeUrl(sourceUrl))
        {
            error = "Only HTTPS YouTube and youtu.be video addresses are supported.";
            return false;
        }
        if(worker_.joinable()) worker_.join();
        std::scoped_lock lock(mutex_);
        const auto runtime = VideoLibrary::Instance().RuntimePath();
        statusPath_ = runtime / "url-probe-status.json";
        cancelPath_ = runtime / "url-probe.cancel";
        std::filesystem::remove(statusPath_);
        std::filesystem::remove(cancelPath_);
        snapshot_ = {};
        snapshot_.state = DownloadState::Probing;
        snapshot_.levelId = levelId;
        snapshot_.message = "Checking YouTube URL";
        snapshot_.metadataOnly = true;
        PaperLogger.info("Checking a YouTube URL for {}", levelId);
        worker_ = std::thread([
            this,
            levelId = std::move(levelId),
            sourceUrl = std::move(sourceUrl)]() mutable
        {
            RunProbe(std::move(levelId), std::move(sourceUrl));
        });
        return true;
    }

    bool DownloadManager::StartUpdaterCheck(bool nightly, bool install, std::string& error)
    {
        if(!initialized_) { error = UnavailableMessage(); return false; }
        {
            std::scoped_lock lock(mutex_);
            RefreshSnapshotFromDiskLocked();
            if(snapshot_.Active()) { error = "A downloader task is already running."; return false; }
        }
        if(worker_.joinable()) worker_.join();
        std::scoped_lock lock(mutex_);
        statusPath_ = VideoLibrary::Instance().RuntimePath() / "update-status.json";
        cancelPath_ = VideoLibrary::Instance().RuntimePath() / "update.cancel";
        std::filesystem::remove(statusPath_);
        snapshot_ = {};
        snapshot_.state = DownloadState::Preparing;
        snapshot_.levelId = "__updater__";
        snapshot_.message = "Checking yt-dlp releases";
        worker_ = std::thread([this, nightly, install]() { RunUpdater(nightly, install); });
        return true;
    }

    void DownloadManager::StartScheduledUpdaterCheck(bool nightly)
    {
        const auto status = VideoLibrary::Instance().RuntimePath() / "update-status.json";
        std::error_code errorCode;
        const auto modified = std::filesystem::last_write_time(status, errorCode);
        if(!errorCode)
        {
            const auto age = std::filesystem::file_time_type::clock::now() - modified;
            if(age < std::chrono::hours(24 * 7)) return;
        }
        std::string error;
        if(!StartUpdaterCheck(nightly, false, error))
            PaperLogger.warn("Scheduled yt-dlp update check skipped: {}", error);
    }

    void DownloadManager::QueueVideoThumbnail(
        std::string levelId,
        std::string sourceUrl,
        std::filesystem::path destination)
    {
        if(!initialized_ || levelId.empty() || sourceUrl.empty() ||
           destination.empty() || std::filesystem::is_regular_file(destination))
            return;

        const auto key = destination.string();
        {
            std::scoped_lock lock(thumbnailMutex_);
            // One attempt per destination per game session prevents a broken
            // or private URL from continuously generating network traffic as
            // the table refreshes and scrolls.
            if(!requestedThumbnails_.emplace(key).second)
                return;
            thumbnailQueue_.push_back({
                std::move(levelId), std::move(sourceUrl), std::move(destination)});
            if(!thumbnailWorker_.joinable())
                thumbnailWorker_ = std::thread([this]() { RunThumbnailQueue(); });
        }
        thumbnailWake_.notify_one();
    }

    void DownloadManager::Cancel()
    {
        std::scoped_lock lock(mutex_);
        if(!snapshot_.Active()) return;
        std::ofstream(cancelPath_, std::ios::binary | std::ios::trunc) << "cancel";
        snapshot_.message = "Stopping download";
        PaperLogger.info("Downloader cancellation requested for {}", snapshot_.levelId);
    }

    DownloadSnapshot DownloadManager::Snapshot()
    {
        std::scoped_lock lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        // The Python worker owns the recovery file while active. Reading and
        // reparsing it once per Unity frame added needless storage and CPU work;
        // 10 Hz is considerably faster than a user can perceive in a progress
        // label while terminal states are still imported directly by workers.
        if(now - lastStatusRefresh_ >= std::chrono::milliseconds(100))
        {
            RefreshSnapshotFromDiskLocked();
            lastStatusRefresh_ = now;
        }
        return snapshot_;
    }

    void DownloadManager::Run(DownloadRequest request, std::filesystem::path finalPath)
    {
        try
        {
            const auto thumbnailPath =
                VideoLibrary::Instance().AllocateThumbnailPath(
                    request.levelId, request.origin);
            rapidjson::Document document(rapidjson::kObjectType);
            auto& allocator = document.GetAllocator();
            AddString(document, "sourceUrl", request.sourceUrl, allocator);
            AddString(document, "finalPath", finalPath.string(), allocator);
            AddString(document, "thumbnailPath", thumbnailPath.string(), allocator);
            AddString(document, "statusPath", statusPath_.string(), allocator);
            AddString(document, "cancelPath", cancelPath_.string(), allocator);
            document.AddMember("explicitContentAllowed", request.explicitContentAllowed, allocator);
            document.AddMember(
                "reserveBytes",
                static_cast<std::uint64_t>(RequiredReserve),
                allocator);
            document.AddMember(
                "unknownRequiredBytes",
                static_cast<std::uint64_t>(1024ull * 1024ull * 1024ull),
                allocator);
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            document.Accept(writer);

            bool runtimeRolledBack = false;
            bool runtimeFailed = false;
            {
                ScopedPythonGil gil;
                auto globals = CreatePythonGlobals(
                    buffer.GetString(), buffer.GetSize());
                PythonObject result;
                if(globals)
                    result.reset(PyRun_String(
                        DownloaderScript,
                        Py_file_input,
                        globals.get(),
                        globals.get()));
                if(!globals || !result)
                {
                    PyErr_Print();
                    PyErr_Clear();
                    // A Python execution/import failure is an internal
                    // downloader failure, unlike a private-video or network
                    // error (the script publishes those normally). Restore the
                    // one retained package and retry this job exactly once.
                    const auto runtime = VideoLibrary::Instance().RuntimePath();
                    const auto active = runtime / "yt-dlp-active";
                    const auto previous = runtime / "yt-dlp-previous";
                    std::error_code fileError;
                    if(globals && std::filesystem::is_regular_file(previous, fileError))
                    {
                        std::string rejectedVersion;
                        std::ifstream version(
                            active.string() + ".version", std::ios::binary);
                        if(version) std::getline(version, rejectedVersion);
                        std::filesystem::remove(active, fileError);
                        if(!fileError)
                            std::filesystem::remove(
                                active.string() + ".version", fileError);
                        if(!fileError)
                            std::filesystem::rename(previous, active, fileError);
                        if(!fileError && std::filesystem::is_regular_file(
                               previous.string() + ".version", fileError))
                            std::filesystem::rename(
                                previous.string() + ".version",
                                active.string() + ".version",
                                fileError);
                        if(!fileError)
                        {
                            std::ofstream rejected(
                                runtime / "yt-dlp-rejected.version",
                                std::ios::binary | std::ios::trunc);
                            rejected << (rejectedVersion.empty()
                                ? "unknown"
                                : rejectedVersion);
                            currentUpdateVersion_ = "2026.07.04";
                            std::ifstream restoredVersion(
                                active.string() + ".version", std::ios::binary);
                            if(restoredVersion)
                                std::getline(
                                    restoredVersion, currentUpdateVersion_);
                            if(PyRun_SimpleString(
                                   "import importlib, sys\n"
                                   "importlib.invalidate_caches()\n"
                                   "[sys.modules.pop(k, None) for k in list(sys.modules) if k == 'bigscreen_jsc_provider' or k == 'yt_dlp' or k.startswith('yt_dlp.') or k == 'yt_dlp_ejs' or k.startswith('yt_dlp_ejs.')]\n") == 0)
                            {
                                result.reset(PyRun_String(
                                    DownloaderScript,
                                    Py_file_input,
                                    globals.get(),
                                    globals.get()));
                                runtimeRolledBack = result != nullptr;
                            }
                            if(!result)
                            {
                                PyErr_Print();
                                PyErr_Clear();
                            }
                        }
                        else
                        {
                            PaperLogger.error(
                                "Could not restore the previous yt-dlp package: {}",
                                fileError.message());
                            ErrorManager::Instance().RecordError(
                                "Restoring the previous yt-dlp package",
                                fileError.message());
                        }
                    }
                }
                runtimeFailed = !result;
            }

            if(runtimeRolledBack)
            {
                std::scoped_lock noticeLock(mutex_);
                updateNotice_ =
                    "yt-dlp encountered an internal runtime error while starting a download. Big Screen restored the previous working downloader and retried the download once.";
            }
            if(runtimeFailed)
            {
                SetFailure(
                    "The embedded downloader could not start. Big Screen recorded the internal error; the map and game can continue normally.");
                return;
            }

            std::scoped_lock lock(mutex_);
            RefreshSnapshotFromDiskLocked();
            if(snapshot_.state == DownloadState::Failed)
            {
                // The status shown in VR stays concise. Persist yt-dlp's raw,
                // sanitized failure and selected-stream facts under the same
                // stable code so a report can be diagnosed without guessing.
                std::ifstream diagnosticStream(statusPath_, std::ios::binary);
                const std::string diagnosticJson{
                    std::istreambuf_iterator<char>(diagnosticStream), {}};
                rapidjson::Document diagnosticStatus;
                diagnosticStatus.Parse(
                    diagnosticJson.data(), diagnosticJson.size());
                const auto code = ReadString(diagnosticStatus, "errorCode");
                const auto detail = ReadString(diagnosticStatus, "diagnostic");
                ErrorManager::Instance().RecordError(
                    code.empty() ? "Downloader failure" : code,
                    detail.empty() ? snapshot_.message : detail);
            }
            if(snapshot_.state == DownloadState::Completed)
            {
                std::ifstream stream(statusPath_, std::ios::binary);
                const std::string json{std::istreambuf_iterator<char>(stream), {}};
                rapidjson::Document status;
                status.Parse(json.data(), json.size());
                StoredVideo stored;
                stored.sourceUrl = request.sourceUrl;
                stored.fileName = finalPath.filename().string();
                stored.title = ReadString(status, "title");
                stored.codec = ReadString(status, "codec");
                stored.offsetSeconds = request.offsetSeconds;
                stored.playbackRate = request.playbackRate;
                stored.fitToSong = request.fitToSong;
                stored.blackDuringLeadIn = request.blackDuringLeadIn;
                stored.durationSeconds = ReadNumber(status, "duration");
                stored.bytes = static_cast<std::uint64_t>(ReadNumber(status, "bytes"));
                stored.width = static_cast<int>(ReadNumber(status, "width"));
                stored.height = static_cast<int>(ReadNumber(status, "height"));
                VideoLibrary::Instance().CommitDownload(
                    request.levelId,
                    request.songName,
                    request.songAuthor,
                    request.origin,
                    std::move(stored));
            }
            PaperLogger.info(
                "Downloader finished for {} with state '{}': {}",
                request.levelId,
                StateName(snapshot_.state),
                snapshot_.message);
        }
        catch(const std::exception& exception)
        {
            SetFailure(std::string("Downloader stopped: ") + exception.what());
        }
        catch(...)
        {
            SetFailure("Downloader stopped because of an unexpected internal error.");
        }
    }

    void DownloadManager::RunProbe(std::string levelId, std::string sourceUrl)
    {
        try
        {
            const auto thumbnailPath =
                VideoLibrary::Instance().RuntimePath() / "url-thumbnail.jpg";
            rapidjson::Document document(rapidjson::kObjectType);
            auto& allocator = document.GetAllocator();
            AddString(document, "sourceUrl", sourceUrl, allocator);
            AddString(document, "statusPath", statusPath_.string(), allocator);
            AddString(document, "thumbnailPath", thumbnailPath.string(), allocator);
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            document.Accept(writer);

            bool runtimeFailed = false;
            {
                ScopedPythonGil gil;
                auto globals = CreatePythonGlobals(
                    buffer.GetString(), buffer.GetSize());
                PythonObject result;
                if(globals)
                    result.reset(PyRun_String(
                        ProbeScript, Py_file_input, globals.get(), globals.get()));
                if(!globals || !result)
                {
                    PyErr_Print();
                    PyErr_Clear();
                    runtimeFailed = true;
                }
            }
            if(runtimeFailed)
            {
                SetFailure(
                    "The embedded downloader could not check this YouTube URL. Big Screen recorded the internal error; try again after restarting Beat Saber.");
                return;
            }

            std::scoped_lock lock(mutex_);
            RefreshSnapshotFromDiskLocked();
            PaperLogger.info(
                "URL check finished for {} with state '{}': {}",
                levelId,
                StateName(snapshot_.state),
                snapshot_.message);
        }
        catch(const std::exception& exception)
        {
            SetFailure(std::string("URL check stopped: ") + exception.what());
        }
        catch(...)
        {
            SetFailure("URL check stopped because of an unexpected internal error.");
        }
    }

    void DownloadManager::RunUpdater(bool nightly, bool install)
    {
        try
        {
            const auto runtime = VideoLibrary::Instance().RuntimePath();
            rapidjson::Document document(rapidjson::kObjectType);
            auto& allocator = document.GetAllocator();
            AddString(document, "statusPath", statusPath_.string(), allocator);
            AddString(document, "nextPath", (runtime / "yt-dlp-next").string(), allocator);
            AddString(document, "currentVersion", currentUpdateVersion_, allocator);
            std::string rejectedVersion;
            std::ifstream rejected(runtime / "yt-dlp-rejected.version", std::ios::binary);
            if(rejected)
                std::getline(rejected, rejectedVersion);
            AddString(document, "rejectedVersion", rejectedVersion, allocator);
            document.AddMember("nightly", nightly, allocator);
            document.AddMember("install", install, allocator);
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            document.Accept(writer);
            bool runtimeFailed = false;
            {
                ScopedPythonGil gil;
                auto globals = CreatePythonGlobals(
                    buffer.GetString(), buffer.GetSize());
                PythonObject result;
                if(globals)
                    result.reset(PyRun_String(
                        UpdaterScript,
                        Py_file_input,
                        globals.get(),
                        globals.get()));
                if(!globals || !result)
                {
                    PyErr_Print();
                    PyErr_Clear();
                    runtimeFailed = true;
                }
            }
            if(runtimeFailed)
            {
                SetFailure(
                    "The embedded downloader could not run the yt-dlp update check. Big Screen recorded the internal error; try again after restarting Beat Saber.");
                return;
            }
            std::scoped_lock lock(mutex_);
            RefreshSnapshotFromDiskLocked();
        }
        catch(const std::exception& exception)
        {
            SetFailure(std::string("yt-dlp update stopped: ") + exception.what());
        }
        catch(...)
        {
            SetFailure("yt-dlp update stopped because of an unexpected internal error.");
        }
    }

    void DownloadManager::RunThumbnailQueue()
    {
        while(true)
        {
            ThumbnailRequest request;
            {
                std::unique_lock lock(thumbnailMutex_);
                thumbnailWake_.wait(lock, [this]() {
                    return stopThumbnailWorker_ || !thumbnailQueue_.empty();
                });
                if(stopThumbnailWorker_ && thumbnailQueue_.empty())
                    return;
                request = std::move(thumbnailQueue_.front());
                thumbnailQueue_.pop_front();
            }

            try
            {
                rapidjson::Document document(rapidjson::kObjectType);
                auto& allocator = document.GetAllocator();
                AddString(document, "sourceUrl", request.sourceUrl, allocator);
                AddString(document, "destination", request.destination.string(), allocator);
                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                document.Accept(writer);

                {
                    ScopedPythonGil gil;
                    auto globals = CreatePythonGlobals(
                        buffer.GetString(), buffer.GetSize());
                    PythonObject result;
                    if(globals)
                        result.reset(PyRun_String(
                            ThumbnailScript,
                            Py_file_input,
                            globals.get(),
                            globals.get()));
                    if(!globals || !result)
                    {
                        PyErr_Clear();
                        PaperLogger.warn(
                            "Could not fetch video thumbnail for {}",
                            request.levelId);
                    }
                    else
                    {
                        PaperLogger.debug(
                            "Cached video thumbnail for {}",
                            request.levelId);
                    }
                }
            }
            catch(const std::exception& error)
            {
                PaperLogger.warn(
                    "Video thumbnail worker failed for {}: {}",
                    request.levelId,
                    error.what());
            }
            catch(...)
            {
                PaperLogger.warn(
                    "Video thumbnail worker failed for {} because of an unexpected internal error",
                    request.levelId);
            }
        }
    }

    void DownloadManager::RefreshSnapshotFromDiskLocked()
    {
        std::ifstream stream(statusPath_, std::ios::binary);
        if(!stream) return;
        const std::string json{std::istreambuf_iterator<char>(stream), {}};
        rapidjson::Document document;
        document.Parse(json.data(), json.size());
        if(document.HasParseError() || !document.IsObject()) return;
        snapshot_.state = ParseState(ReadString(document, "state"));
        snapshot_.message = ReadString(document, "message");
        snapshot_.downloadedBytes = static_cast<std::uint64_t>(ReadNumber(document, "downloadedBytes"));
        snapshot_.totalBytes = static_cast<std::uint64_t>(ReadNumber(document, "totalBytes"));
        snapshot_.speedBytesPerSecond = ReadNumber(document, "speed");
        snapshot_.etaSeconds = ReadNumber(document, "eta");
        const auto title = ReadString(document, "title");
        const auto thumbnailPath = ReadString(document, "thumbnailPath");
        if(!title.empty()) snapshot_.title = title;
        if(!thumbnailPath.empty()) snapshot_.thumbnailPath = thumbnailPath;
    }

    void DownloadManager::SetFailure(std::string message)
    {
        std::scoped_lock lock(mutex_);
        snapshot_.state = DownloadState::Failed;
        snapshot_.message = std::move(message);
        PaperLogger.error("{}", snapshot_.message);
        ErrorManager::Instance().RecordError(
            "Downloader operation",
            snapshot_.message);
    }
}
