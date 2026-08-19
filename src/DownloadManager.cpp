// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2026 Loud160 (AKA Whisp) and the Big Screen contributors
//
// Part of Big Screen.
// Distributed under GPL-3.0-only with additional terms under GPLv3
// section 7(b)/(c) and an interoperability permission under section 7;
// see LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "BigScreen/DownloadManager.hpp"
#include "BigScreen/Utility.hpp"

#include <Python.h>
#include <dlfcn.h>
#include <pthread.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <unordered_set>

#include "main.hpp"
#include "BigScreen/CoreLogic.hpp"
#include "BigScreen/DownloaderActivation.hpp"
#include "BigScreen/ErrorManager.hpp"
#include "BigScreen/DiagnosticSessionLogger.hpp"
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

        using DownloaderPackage::Activation;
        using DownloaderPackage::NormalizeChannel;
        using DownloaderPackage::VersionIsOlder;

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
            }

            // Every failed formatting attempt leaves its own Python exception.
            // Clear it before asking PyObject_Str to describe the original.
            PyErr_Clear();
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
            PythonObject builtins{PyImport_ImportModule("builtins")};
            if(!builtins || PyDict_SetItemString(
                   globals.get(), "__builtins__", builtins.get()) < 0)
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

        std::filesystem::path IncomingSibling(
            const std::filesystem::path& destination)
        {
            return destination.parent_path() /
                (destination.stem().string() + ".incoming" +
                 destination.extension().string());
        }

        /// Atomically promotes a fully downloaded sibling while retaining the
        /// old destination until its manifest commit succeeds. This closes the
        /// subtle same-codec replacement hole where yt-dlp's overwrite option
        /// could destroy the current 720p/1080p MP4 before a failed replacement
        /// had produced a usable new file.
        class StagedFileReplacement final {
        public:
            StagedFileReplacement(
                std::filesystem::path incoming,
                std::filesystem::path destination)
                : incoming_(std::move(incoming)),
                  destination_(std::move(destination)),
                  backup_(destination_.string() + ".replacement-backup") {}

            void Promote()
            {
                std::error_code error;
                if(!std::filesystem::is_regular_file(incoming_, error) || error)
                    throw std::runtime_error(
                        "The completed download staging file is missing.");

                const bool backupExists =
                    std::filesystem::is_regular_file(backup_, error) && !error;
                error.clear();
                hadDestination_ =
                    std::filesystem::is_regular_file(destination_, error) && !error;
                if(backupExists)
                {
                    if(hadDestination_)
                        std::filesystem::remove(backup_, error);
                    else
                        std::filesystem::rename(backup_, destination_, error);
                    if(error)
                        throw std::runtime_error(
                            "Could not recover the previous video before replacement: " +
                            error.message());
                    hadDestination_ = true;
                }

                if(hadDestination_)
                {
                    std::filesystem::rename(destination_, backup_, error);
                    if(error)
                        throw std::runtime_error(
                            "Could not preserve the current video before replacement: " +
                            error.message());
                }
                std::filesystem::rename(incoming_, destination_, error);
                if(error)
                {
                    Restore();
                    throw std::runtime_error(
                        "Could not publish the completed replacement video: " +
                        error.message());
                }
                promoted_ = true;
            }

            void Commit() noexcept
            {
                committed_ = true;
                std::error_code ignored;
                std::filesystem::remove(backup_, ignored);
            }

            ~StagedFileReplacement()
            {
                if(promoted_ && !committed_)
                    Restore();
            }

            StagedFileReplacement(const StagedFileReplacement&) = delete;
            StagedFileReplacement& operator=(
                const StagedFileReplacement&) = delete;

        private:
            void Restore() noexcept
            {
                std::error_code ignored;
                std::filesystem::remove(destination_, ignored);
                if(hadDestination_)
                    std::filesystem::rename(backup_, destination_, ignored);
                promoted_ = false;
            }

            std::filesystem::path incoming_;
            std::filesystem::path destination_;
            std::filesystem::path backup_;
            bool hadDestination_ = false;
            bool promoted_ = false;
            bool committed_ = false;
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

        std::vector<int> ReadIntegerArray(
            const rapidjson::Value& object,
            const char* name)
        {
            std::vector<int> values;
            if(!object.IsObject()) return values;
            const auto member = object.FindMember(name);
            if(member == object.MemberEnd() || !member->value.IsArray())
                return values;
            for(const auto& item : member->value.GetArray())
            {
                if(item.IsInt()) values.push_back(item.GetInt());
            }
            return values;
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
            if(!Utility::IsRegularFile(path))
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

        // Shared by the full transfer and metadata probe. Keeping codec/range
        // policy in one Python fragment prevents one surface from accepting a
        // tier the other later rejects after yt-dlp format fields change.
        constexpr std::string_view MediaScriptHelpers = R"PY(
def clean_error(value):
    return re.sub(r'\x1b\[[0-9;]*m', '', str(value)).strip()[-700:]

def is_sdr(candidate):
    dynamic_range = str(candidate.get('dynamic_range') or 'SDR').upper()
    note = str(candidate.get('format_note') or '').upper()
    codec = str(candidate.get('vcodec') or '').lower()
    return ('HDR' not in dynamic_range and 'HDR' not in note and
            '.02' not in codec)

def tier(candidate):
    width = int(candidate.get('width') or 0)
    height = int(candidate.get('height') or 0)
    return min(width, height) if width > 0 and height > 0 else 0

)PY";

        constexpr const char* DownloaderScript = R"PY(
import json, os, re, shutil, time, traceback, urllib.parse, urllib.request

job = json.loads(BIGSCREEN_JOB)
status_path = job['statusPath']
cancel_path = job['cancelPath']
ytdlp_log_path = job.get('ytdlpLogPath') or ''

def sanitize_ytdlp_message(value):
    text = re.sub(r'\x1b\[[0-9;]*m', '', str(value)).strip()
    text = re.sub(r'(?i)(authorization|cookie|po[_-]?token)\s*[:=]\s*\S+',
                  r'\1=[redacted]', text)
    text = re.sub(r'https?://[^\s\x27\x22?#]+[?#][^\s\x27\x22]*',
                  lambda match: match.group(0).split('?', 1)[0].split('#', 1)[0] + '?[redacted]', text)
    return text[-2048:]

class BigScreenYtDlpLogger:
    def __init__(self):
        self.last = None
    def _write(self, severity, value):
        if not ytdlp_log_path:
            return
        message = sanitize_ytdlp_message(value)
        signature = (severity, message)
        if not message or signature == self.last:
            return
        self.last = signature
        with open(ytdlp_log_path, 'a', encoding='utf-8') as stream:
            stream.write(json.dumps({
                'severity': severity,
                'message': message,
                'time': time.time()}, ensure_ascii=False) + '\n')
            stream.flush()
    def debug(self, value):
        # yt-dlp routes ordinary information through debug(). Keep only useful
        # selected-format facts; block chatter is represented by progress.
        text = str(value)
        lower = text.lower()
        if 'format' in lower and ('selected' in lower or 'download' in lower):
            self._write('info', text)
    def info(self, value):
        self._write('info', value)
    def warning(self, value):
        self._write('warning', value)
    def error(self, value):
        self._write('error', value)

def publish(state, message='', durable=True, **values):
    data = {'state': state, 'message': message}
    data.update(values)
    # C++ polls this file concurrently. Replace a flushed sibling file so it
    # can observe either complete status document, never partial JSON.
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
        return 'failed', 'The requested video resolution and codec are not available.'
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
    common = {
        'quiet': True,
        # Warnings are routed only to BigScreenYtDlpLogger, not stdout. This
        # keeps useful support evidence without restoring noisy console output.
        'no_warnings': False,
        'noplaylist': True,
        'continuedl': True,
        'nopart': False,
        'progress_hooks': [progress],
        'logger': BigScreenYtDlpLogger(),
        'overwrites': True,
        # Cancellation is observed by progress hooks. Bound individual network
        # waits and retries so a disconnected Quest can still leave Beat Saber
        # or shut down without waiting indefinitely for a Python worker.
        'socket_timeout': 15,
        'retries': 3,
        'fragment_retries': 3,
        'extractor_retries': 3,
        # WORKAROUND (August 2026): yt-dlp 2026.07.04 selected android_vr by
        # default. YouTube began
        # requiring a Google Video Server PO token for that client's media
        # URLs in August 2026, which made otherwise public videos fail with a
        # mid-transfer HTTP 403. Let the current extractor choose its supported
        # client set while explicitly excluding android_vr. The pinned nightly
        # baseline currently selects VISIONOS and can expose fragmented HLS
        # streams that avoid the rejected Android-VR URL path.
        'extractor_args': {
            'youtube': {'player_client': ['default', '-android_vr']}},
    }
    with yt_dlp.YoutubeDL(dict(common, skip_download=True)) as probe:
        info = probe.extract_info(job['sourceUrl'], download=False)
    age_limit = int(info.get('age_limit') or 0)
    if age_limit >= 18 and not job.get('explicitContentAllowed', False):
        raise PermissionError('Big Screen blocked this age-restricted video because explicit content is disabled in Beat Saber parental controls.')
    requested_height = int(job.get('requestedHeight') or 1080)
    maximum_fps = max(1, int(job.get('maximumSourceFps') or 30))
    if requested_height < 1 or requested_height > 1440:
        raise RuntimeError('Requested format is not available: invalid resolution tier')

    formats = []
    for candidate in info.get('formats') or []:
        width = int(candidate.get('width') or 0)
        height = int(candidate.get('height') or 0)
        codec = str(candidate.get('vcodec') or '').lower()
        if width <= 0 or height <= 0 or candidate.get('acodec') != 'none' or not is_sdr(candidate):
            continue
        if requested_height == 1440:
            compatible = (candidate.get('ext') == 'webm' and
                (codec.startswith('vp9') or codec.startswith('vp09.00')))
        else:
            compatible = (candidate.get('ext') == 'mp4' and
                codec.startswith('avc1'))
        if compatible and tier(candidate) == requested_height:
            formats.append(candidate)
    if not formats:
        raise RuntimeError('Requested format is not available: no compatible %dp stream' % requested_height)
    within_fps_limit = [candidate for candidate in formats
        if float(candidate.get('fps') or 0) <= maximum_fps + 0.01]
    selection_pool = within_fps_limit or formats
    chosen = max(selection_pool, key=lambda f: (
        float(f.get('fps') or 0) if within_fps_limit else -float(f.get('fps') or 0),
        float(f.get('tbr') or 0),
        int(f.get('filesize') or f.get('filesize_approx') or 0)))
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
    # library. Derive the pinned image host from YouTube's validated video id
    # instead of trusting a metadata-provided arbitrary URL. A transient image
    # failure preserves the last known-good thumbnail and never fails video.
    thumbnail_path = job['thumbnailPath']
    published_thumbnail = ''
    try:
        video_id = str(result.get('id') or info.get('id') or '')
        if re.fullmatch(r'[A-Za-z0-9_-]{11}', video_id):
            thumbnail_url = 'https://i.ytimg.com/vi/' + video_id + '/hqdefault.jpg'
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
    except BaseException as thumbnail_error:
        try:
            if os.path.exists(thumbnail_path + '.tmp'):
                os.remove(thumbnail_path + '.tmp')
        except OSError:
            pass
        # Keep the established thumbnail. Its failure detail is appended to
        # the terminal status for the persistent log without confusing the
        # user by changing a successful video download into a failed one.
        published_thumbnail = thumbnail_path if os.path.exists(thumbnail_path) else ''
        retry_detail += 'Thumbnail warning: ' + clean_error(thumbnail_error) + '. '

    publish(
        'completed',
        'Video downloaded',
        title=result.get('title') or info.get('title') or '',
        duration=result.get('duration') or info.get('duration') or 0,
        width=chosen.get('width') or 0,
        height=chosen.get('height') or 0,
        codec=chosen.get('vcodec') or 'h264',
        requestedHeight=requested_height,
        thumbnailPath=published_thumbnail,
        diagnostic=retry_detail.strip(),
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

        // BeatSaver map packages use the same private CPython worker as video
        // downloads, but never import yt-dlp. Keeping network and ZIP work off
        // Unity's main thread prevents the showcase button from stalling menu
        // rendering. The script accepts only the exact requested BeatSaver
        // revision and extracts through a bounded, traversal-safe staging
        // directory before atomically publishing the managed map folder.
        constexpr const char* MapPackageScript = R"PY(
import json, os, shutil, stat, time, urllib.parse, urllib.request, zipfile

job = json.loads(BIGSCREEN_JOB)
status_path = job['statusPath']
cancel_path = job['cancelPath']

def publish(state, message='', durable=True, **values):
    data = {'state': state, 'message': message}
    data.update(values)
    # C++ polls this file concurrently. Replace a flushed sibling file so it
    # can observe either complete status document, never partial JSON.
    temporary = status_path + '.tmp'
    with open(temporary, 'w', encoding='utf-8') as stream:
        json.dump(data, stream, ensure_ascii=False)
        if durable:
            stream.flush()
            os.fsync(stream.fileno())
    os.replace(temporary, status_path)

def cancelled():
    if os.path.exists(cancel_path):
        raise RuntimeError('BIGSCREEN_CANCELLED')

def safe_https_url(value, allowed_hosts):
    parsed = urllib.parse.urlparse(str(value or ''))
    return (parsed.scheme.lower() == 'https' and
            (parsed.hostname or '').lower() in allowed_hosts)

archive_path = job['archivePath']
staging_path = job['stagingPath']
final_path = job['destinationDirectory']

try:
    publish('preparing', 'Finding exact BeatSaver map revision')
    cancelled()
    map_key = str(job['mapKey'])
    expected_hash = str(job['expectedHash']).lower()
    api_url = 'https://api.beatsaver.com/maps/id/' + urllib.parse.quote(
        map_key, safe='')
    request = urllib.request.Request(
        api_url,
        headers={'User-Agent': 'Big-Screen-Beat-Saber/' + str(job['modVersion'])})
    with urllib.request.urlopen(request, timeout=20) as response:
        if not safe_https_url(response.geturl(), {'api.beatsaver.com'}):
            raise RuntimeError('BeatSaver metadata redirected to an unexpected host.')
        metadata_bytes = response.read(2 * 1024 * 1024 + 1)
    if len(metadata_bytes) > 2 * 1024 * 1024:
        raise RuntimeError('BeatSaver returned unexpectedly large map metadata.')
    metadata = json.loads(metadata_bytes.decode('utf-8'))
    revision = next(
        (item for item in metadata.get('versions', [])
         if str(item.get('hash') or '').lower() == expected_hash),
        None)
    if revision is None:
        raise RuntimeError(
            'BeatSaver no longer lists the exact showcase map revision.')
    download_url = revision.get('downloadURL') or revision.get('downloadUrl')
    allowed_cdn_hosts = {
        'r2cdn.beatsaver.com', 'cdn.beatsaver.com', 'beatsaver.com'}
    if not safe_https_url(download_url, allowed_cdn_hosts):
        raise RuntimeError('BeatSaver returned an unexpected map download host.')

    os.makedirs(os.path.dirname(final_path), exist_ok=True)
    free = shutil.disk_usage(os.path.dirname(final_path)).free
    if free < int(job['requiredFreeBytes']):
        raise OSError(
            'Not enough free Quest storage to install the showcase map and video.')

    publish('downloading', 'Downloading showcase map')
    request = urllib.request.Request(
        download_url,
        headers={'User-Agent': 'Big-Screen-Beat-Saber/' + str(job['modVersion'])})
    downloaded = 0
    with urllib.request.urlopen(request, timeout=20) as response:
        if not safe_https_url(response.geturl(), allowed_cdn_hosts):
            raise RuntimeError('BeatSaver map download redirected to an unexpected host.')
        total = int(response.headers.get('Content-Length') or 0)
        if total > int(job['maximumArchiveBytes']):
            raise RuntimeError('The BeatSaver map package is unexpectedly large.')
        with open(archive_path, 'wb') as archive:
            last_publish = 0.0
            while True:
                cancelled()
                block = response.read(128 * 1024)
                if not block:
                    break
                archive.write(block)
                downloaded += len(block)
                if downloaded > int(job['maximumArchiveBytes']):
                    raise RuntimeError('The BeatSaver map package exceeded its safe size limit.')
                now = time.monotonic()
                if now - last_publish >= 0.125:
                    last_publish = now
                    publish(
                        'downloading', 'Downloading showcase map', durable=False,
                        downloadedBytes=downloaded, totalBytes=total)
            archive.flush()
            os.fsync(archive.fileno())

    cancelled()
    publish('preparing', 'Installing showcase map')
    if os.path.isdir(staging_path):
        shutil.rmtree(staging_path)
    os.makedirs(staging_path)
    staging_root = os.path.realpath(staging_path)
    entry_count = 0
    expanded_bytes = 0
    with zipfile.ZipFile(archive_path) as package:
        for entry in package.infolist():
            cancelled()
            entry_count += 1
            if entry_count > int(job['maximumEntries']):
                raise RuntimeError('The BeatSaver package contains too many files.')
            normalized = entry.filename.replace('\\', '/')
            if entry.is_dir():
                normalized = normalized.rstrip('/')
            if (not normalized or normalized.startswith('/') or
                    any(part in ('', '.', '..') for part in normalized.split('/'))):
                raise RuntimeError('The BeatSaver package contains an unsafe file path.')
            unix_mode = (entry.external_attr >> 16) & 0o170000
            if unix_mode == stat.S_IFLNK:
                raise RuntimeError('The BeatSaver package contains an unsupported symbolic link.')
            expanded_bytes += int(entry.file_size)
            if expanded_bytes > int(job['maximumExpandedBytes']):
                raise RuntimeError('The expanded BeatSaver map is unexpectedly large.')
            destination = os.path.realpath(os.path.join(staging_root, normalized))
            if os.path.commonpath((staging_root, destination)) != staging_root:
                raise RuntimeError('The BeatSaver package tried to write outside its map folder.')
            if entry.is_dir():
                os.makedirs(destination, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            with package.open(entry) as source, open(destination, 'wb') as target:
                shutil.copyfileobj(source, target, length=128 * 1024)

    # BeatSaver archives are normally rooted directly at Info.dat, but accept
    # one harmless wrapper directory while rejecting ambiguous layouts.
    def find_info(root):
        direct = [name for name in os.listdir(root)
                  if name.lower() in ('info.dat', 'info.json') and
                  os.path.isfile(os.path.join(root, name))]
        if direct:
            return root, direct[0]
        children = [name for name in os.listdir(root)
                    if os.path.isdir(os.path.join(root, name))]
        if len(children) == 1:
            child = os.path.join(root, children[0])
            nested = [name for name in os.listdir(child)
                      if name.lower() in ('info.dat', 'info.json') and
                      os.path.isfile(os.path.join(child, name))]
            if nested:
                return child, nested[0]
        raise RuntimeError('The BeatSaver package does not contain a valid map root.')

    map_root, info_name = find_info(staging_root)
    with open(os.path.join(map_root, info_name), encoding='utf-8-sig') as stream:
        info = json.load(stream)
    def declared_file_exists(name):
        normalized = str(name or '').replace('\\', '/')
        if (not normalized or normalized.startswith('/') or
                any(part in ('', '.', '..') for part in normalized.split('/'))):
            return False
        candidate = os.path.realpath(os.path.join(map_root, normalized))
        return (os.path.commonpath((os.path.realpath(map_root), candidate)) ==
                os.path.realpath(map_root) and os.path.isfile(candidate))
    song_file = info.get('_songFilename') or info.get('song', {}).get('songFilename')
    if not declared_file_exists(song_file):
        raise RuntimeError('The showcase map is missing its declared song audio.')
    lawless_file = ''
    for beatmap_set in info.get('_difficultyBeatmapSets', []):
        if str(beatmap_set.get('_beatmapCharacteristicName') or '').lower() != 'lawless':
            continue
        for beatmap in beatmap_set.get('_difficultyBeatmaps', []):
            if str(beatmap.get('_difficulty') or '').lower() == 'expertplus':
                lawless_file = beatmap.get('_beatmapFilename') or ''
    if not declared_file_exists(lawless_file):
        raise RuntimeError('The showcase package is missing Lawless Expert+ data.')

    # If the archive had a wrapper directory, promote its contents rather than
    # installing another level of nesting that SongCore cannot identify.
    publish_root = staging_root
    if os.path.realpath(map_root) != staging_root:
        promoted = staging_path + '.promoted'
        if os.path.isdir(promoted):
            shutil.rmtree(promoted)
        os.replace(map_root, promoted)
        shutil.rmtree(staging_root)
        os.replace(promoted, staging_root)
        publish_root = staging_root
    if os.path.isdir(final_path):
        shutil.rmtree(final_path)
    os.replace(publish_root, final_path)
    publish(
        'completed', 'Showcase map installed',
        bytes=expanded_bytes,
        downloadedBytes=downloaded,
        totalBytes=downloaded)
except BaseException as error:
    message = str(error).strip()
    if 'BIGSCREEN_CANCELLED' in message:
        publish('cancelled', 'Showcase download cancelled')
    else:
        publish(
            'failed',
            'Showcase map download failed: ' + message + ' (BS-DEMO-MAP-001)',
            errorCode='BS-DEMO-MAP-001', diagnostic=message)
finally:
    for path in (archive_path, staging_path, staging_path + '.promoted'):
        try:
            if os.path.isdir(path):
                shutil.rmtree(path)
            elif os.path.exists(path):
                os.remove(path)
        except OSError:
            pass
)PY";

        constexpr const char* ProbeScript = R"PY(
import json, os, re, urllib.request

job = json.loads(BIGSCREEN_JOB)
status_path = job['statusPath']

def cancelled():
    if os.path.exists(job['cancelPath']):
        raise KeyboardInterrupt('Video URL check cancelled')

def publish(state, message='', **values):
    data = {'state': state, 'message': message}
    data.update(values)
    # Never expose a half-written status document to the C++ UI. Flush the
    # sibling temporary file completely, then atomically replace the old file.
    temporary = status_path + '.tmp'
    with open(temporary, 'w', encoding='utf-8') as stream:
        json.dump(data, stream, ensure_ascii=False)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, status_path)

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
    cancelled()
    import bigscreen_jsc_provider
    import yt_dlp
    with yt_dlp.YoutubeDL({
            'quiet': True,
            'no_warnings': True,
            'noplaylist': True,
            'skip_download': True,
            # Keep metadata probing on the same August 2026 workaround/client
            # policy as the eventual
            # transfer. Otherwise the UI can offer Android-VR-only formats
            # that the download worker deliberately refuses to use.
            'extractor_args': {
                'youtube': {'player_client': ['default', '-android_vr']}}}) as probe:
        info = probe.extract_info(job['sourceUrl'], download=False)
    cancelled()
    video_id = str(info.get('id') or '')
    title = str(info.get('title') or 'YouTube video')
    if not video_id:
        raise RuntimeError('YouTube did not return a video identifier.')

    h264_heights = set()
    vp9_heights = set()
    for candidate in info.get('formats') or []:
        codec = str(candidate.get('vcodec') or '').lower()
        height = tier(candidate)
        if height <= 0 or candidate.get('acodec') != 'none' or not is_sdr(candidate):
            continue
        if candidate.get('ext') == 'mp4' and codec.startswith('avc1'):
            h264_heights.add(height)
        if (candidate.get('ext') == 'webm' and
                (codec.startswith('vp9') or codec.startswith('vp09.00'))):
            vp9_heights.add(height)

    available_heights = [height for height in (480, 720, 1080)
        if height in h264_heights]
    if 1440 in vp9_heights:
        available_heights.append(1440)
    # Preserve unusual and old uploads: when none of the supported standard
    # tiers exists, offer the single best real H.264 height below 480p.
    if not available_heights:
        lower = [height for height in h264_heights if height < 480]
        if lower:
            available_heights = [max(lower)]
    if not available_heights:
        raise RuntimeError('No compatible 8-bit SDR H.264 or VP9 download tier is available.')

    # Thumbnail artwork is decorative. A CDN/certificate/image failure must
    # not discard the valid title and compatible resolution tiers above.
    published_thumbnail = ''
    temporary = job['thumbnailPath'] + '.tmp'
    try:
        thumbnail_url = 'https://i.ytimg.com/vi/' + video_id + '/hqdefault.jpg'
        request = urllib.request.Request(
            thumbnail_url,
            headers={'User-Agent': 'Big-Screen-Beat-Saber'})
        with urllib.request.urlopen(request, timeout=20) as response:
            image = response.read(4 * 1024 * 1024 + 1)
        cancelled()
        if len(image) > 4 * 1024 * 1024:
            raise RuntimeError('The YouTube thumbnail was unexpectedly large.')
        with open(temporary, 'wb') as stream:
            stream.write(image)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, job['thumbnailPath'])
        published_thumbnail = job['thumbnailPath']
    except KeyboardInterrupt:
        raise
    except BaseException:
        try:
            if os.path.exists(temporary):
                os.remove(temporary)
        except OSError:
            pass
    publish(
        'probe_completed',
        'Recognized: ' + title,
        title=title,
        thumbnailPath=published_thumbnail,
        availableHeights=available_heights)
except KeyboardInterrupt:
    publish('cancelled', 'Video URL check cancelled')
except BaseException as error:
    publish(
        'failed', classify(error), errorCode='BS-DL-PROBE-001',
        diagnostic=clean_error(error))
)PY";

        constexpr const char* UpdaterScript = R"PY(
import hashlib, json, os, urllib.request, zipfile
job = json.loads(BIGSCREEN_JOB)
def version_key(value):
    try:
        return tuple(int(part) for part in str(value).split('.'))
    except (TypeError, ValueError):
        return ()
def cancelled():
    if os.path.exists(job['cancelPath']):
        raise KeyboardInterrupt('yt-dlp update cancelled')
def publish(state, message='', **extra):
    value = {'state': state, 'message': message}; value.update(extra)
    # Atomically publish complete updater status to the polling C++ reader.
    temporary = job['statusPath'] + '.tmp'
    with open(temporary, 'w', encoding='utf-8') as stream:
        json.dump(value, stream); stream.flush(); os.fsync(stream.fileno())
    os.replace(temporary, job['statusPath'])
try:
    publish('preparing', 'Checking yt-dlp releases')
    cancelled()
    repository = 'yt-dlp/yt-dlp-nightly-builds' if job['nightly'] else 'yt-dlp/yt-dlp'
    request = urllib.request.Request(
        'https://api.github.com/repos/' + repository + '/releases/latest',
        headers={'User-Agent': 'Big-Screen-Beat-Saber'})
    with urllib.request.urlopen(request, timeout=30) as response:
        release = json.load(response)
    cancelled()
    version = str(release.get('tag_name') or '')
    current = job['currentVersion']
    rejected = job.get('rejectedVersion', '')
    latest_key = version_key(version)
    current_key = version_key(current)
    channel_switch = bool(job.get('channelSwitch'))
    selected_channel = 'nightly' if job['nightly'] else 'stable'
    if version and version == rejected:
        publish('up_to_date', 'yt-dlp ' + version + ' was rejected on this headset. Big Screen will wait for a newer release.', version=version)
    elif not channel_switch and (version == current or (latest_key and current_key and latest_key <= current_key)):
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
        cancelled()
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
                cancelled()
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
        with open(job['nextPath'] + '.channel', 'w', encoding='utf-8') as channel_file:
            channel_file.write(selected_channel)
        publish('completed', 'yt-dlp ' + version + ' verified. Restart Beat Saber to activate it.', version=version)
except KeyboardInterrupt:
    publish('cancelled', 'yt-dlp update cancelled')
except BaseException as error:
    temporary_path = locals().get('temporary', '')
    if temporary_path and os.path.exists(temporary_path):
        try:
            os.remove(temporary_path)
        except OSError:
            pass
    publish('failed', 'yt-dlp update failed: ' + str(error)[-700:])
)PY";

        // Release discovery is independent from download/install operations.
        // It runs on its own worker so opening Big Screen, browsing songs, and
        // starting a video download never wait for GitHub. Automatic nightly
        // checks ask stable first and contact the nightly repository only when
        // no newer stable release can replace the installed nightly.
        constexpr const char* YtDlpReleaseScript = R"PY(
import json, urllib.error, urllib.request

job = json.loads(BIGSCREEN_JOB)
def version_key(value):
    try:
        return tuple(int(part) for part in str(value).split('.'))
    except (TypeError, ValueError):
        return ()
def newer(candidate, current):
    left, right = version_key(candidate), version_key(current)
    return bool(left and right and left > right)
def stable_has_caught_up(stable, nightly):
    left, right = version_key(stable), version_key(nightly)
    # Stable tags use YYYY.MM.DD while nightly tags append a build suffix.
    # Compare their release dates: a stable cut on the same date counts as
    # caught up, while tuple-comparing all fields would incorrectly treat the
    # shorter stable tag as older forever.
    return bool(len(left) >= 3 and len(right) >= 3 and left[:3] >= right[:3])
def latest(repository):
    request = urllib.request.Request(
        'https://api.github.com/repos/' + repository + '/releases/latest',
        headers={
            'Accept': 'application/vnd.github+json',
            'User-Agent': 'Big-Screen-Beat-Saber',
            'X-GitHub-Api-Version': '2022-11-28',
        })
    with urllib.request.urlopen(request, timeout=20) as response:
        payload = response.read(1024 * 1024 + 1)
    if len(payload) > 1024 * 1024:
        raise RuntimeError('GitHub returned an unexpectedly large release response.')
    value = str(json.loads(payload.decode('utf-8')).get('tag_name') or '')
    if not version_key(value):
        raise RuntimeError('GitHub returned an unrecognized yt-dlp version.')
    return value

result = {
    'state': 'unavailable',
    'message': 'The yt-dlp release check did not complete.',
    'stableVersion': '',
    'nightlyVersion': '',
    'checkedChannel': '',
    'availableVersion': '',
    'stableReturn': False,
    'stableCaughtUp': False,
}
try:
    current = str(job.get('currentVersion') or '')
    current_channel = str(job.get('currentChannel') or 'stable')
    requested_nightly = bool(job.get('requestedNightly'))
    automatic = bool(job.get('automatic'))
    stable = latest('yt-dlp/yt-dlp')
    result['stableVersion'] = stable

    if current_channel == 'nightly' and (automatic or requested_nightly):
        # A newer-dated stable release wins. Only if stable has not caught up
        # do we spend a second request checking the nightly channel.
        if stable_has_caught_up(stable, current):
            result.update(
                state='update_available', checkedChannel='stable',
                availableVersion=stable, stableReturn=True,
                stableCaughtUp=True,
                message='A newer stable yt-dlp release is available.')
        else:
            nightly = latest('yt-dlp/yt-dlp-nightly-builds')
            result['nightlyVersion'] = nightly
            if newer(nightly, current):
                result.update(
                    state='update_available', checkedChannel='nightly',
                    availableVersion=nightly,
                    message='A newer nightly yt-dlp build is available.')
            else:
                result.update(
                    state='up_to_date', checkedChannel='nightly',
                    message='The installed nightly yt-dlp build is current; stable has not caught up yet.')
    elif requested_nightly:
        # This path is manual only: automatic checks never move a stable user
        # to nightly. A different nightly package is offered as an explicit
        # channel switch even when date ordering is unusual.
        nightly = latest('yt-dlp/yt-dlp-nightly-builds')
        result['nightlyVersion'] = nightly
        if nightly != current:
            result.update(
                state='update_available', checkedChannel='nightly',
                availableVersion=nightly,
                message='A nightly yt-dlp build is available.')
        else:
            result.update(
                state='up_to_date', checkedChannel='nightly',
                message='The installed nightly yt-dlp build is current.')
    else:
        # Explicit stable checks can intentionally return from nightly even if
        # the stable tag sorts lower. Installation treats that as a channel
        # switch rather than an ordinary upgrade.
        returning_to_stable = current_channel == 'nightly'
        caught_up = (not returning_to_stable or
                     stable_has_caught_up(stable, current))
        available = newer(stable, current) or returning_to_stable
        result.update(
            state='update_available' if available else 'up_to_date',
            checkedChannel='stable',
            availableVersion=stable if available else '',
            stableReturn=returning_to_stable,
            stableCaughtUp=caught_up,
            message=(('Stable has caught up with the installed nightly build.'
                      if caught_up else
                      'Stable has not caught up with the installed nightly build.')
                     if returning_to_stable else
                     ('A stable yt-dlp release is available.' if available
                      else 'The installed stable yt-dlp release is current.'))
                    )
except urllib.error.HTTPError as error:
    result['message'] = ('GitHub temporarily limited yt-dlp checks.'
                         if error.code in (403, 429)
                         else 'GitHub could not check yt-dlp releases (HTTP ' + str(error.code) + ').')
except urllib.error.URLError:
    result['message'] = 'Could not reach GitHub. Check the Quest network connection and try again.'
except BaseException as error:
    result['message'] = 'Could not check yt-dlp releases: ' + str(error)[-300:]

BIGSCREEN_YTDLP_RELEASE_RESULT = json.dumps(result)
)PY";

        // Big Screen updates are notification-only. The latest-release API
        // intentionally excludes drafts and prereleases, and this request is
        // unauthenticated so the mod never asks a player for a GitHub token.
        // GitHub returns 404 for an unauthenticated private repository; that is
        // an expected unavailable state while development remains private.
        constexpr const char* ModReleaseScript = R"PY(
import json, urllib.error, urllib.request

result = {'state': 'unavailable', 'message': 'The release check did not complete.'}
try:
    request = urllib.request.Request(
        'https://api.github.com/repos/Loud160/BigScreen/releases/latest',
        headers={
            'Accept': 'application/vnd.github+json',
            'User-Agent': 'Big-Screen-Beat-Saber',
            'X-GitHub-Api-Version': '2022-11-28',
        })
    with urllib.request.urlopen(request, timeout=15) as response:
        payload = response.read(1024 * 1024 + 1)
    if len(payload) > 1024 * 1024:
        raise RuntimeError('GitHub returned an unexpectedly large release response.')
    release = json.loads(payload.decode('utf-8'))
    result = {
        'state': 'received',
        'version': str(release.get('tag_name') or ''),
    }
except urllib.error.HTTPError as error:
    if error.code == 404:
        result = {
            'state': 'unavailable',
            'message': 'No public Big Screen release is available yet. The GitHub repository may still be private.',
        }
    elif error.code in (403, 429):
        result = {
            'state': 'unavailable',
            'message': 'GitHub temporarily limited update checks. Try again later.',
        }
    else:
        result = {
            'state': 'unavailable',
            'message': 'GitHub could not check Big Screen releases (HTTP ' + str(error.code) + ').',
        }
except urllib.error.URLError:
    result = {
        'state': 'unavailable',
        'message': 'Could not reach GitHub. Check the Quest network connection and try again.',
    }
except BaseException as error:
    result = {
        'state': 'unavailable',
        'message': 'Could not check Big Screen releases: ' + str(error)[-300:],
    }

BIGSCREEN_RELEASE_RESULT = json.dumps(result)
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
        {
            std::scoped_lock lock(operationMutex_);
            stopOperationWorker_ = true;
            pendingOperation_ = {};
        }
        {
            std::scoped_lock lock(statusWaitMutex_);
            stopStatusWorker_ = true;
        }
        operationWake_.notify_all();
        statusWake_.notify_all();
        // Singleton destruction occurs during process shutdown, never from a
        // menu callback. This is the only join for the persistent downloader
        // worker; ordinary UI operations never wait for Python/network work.
        if(worker_.joinable()) worker_.join();
        if(statusWorker_.joinable()) statusWorker_.join();
        if(modReleaseWorker_.joinable()) modReleaseWorker_.join();
        if(ytDlpReleaseWorker_.joinable()) ytDlpReleaseWorker_.join();
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

        // Validate and publish the certificate path before CPython starts. A
        // failure here receives a stable initialization code; Big Screen never
        // attempts to create a second interpreter in the same game process.
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

        // A dynamically installed package normally takes precedence over the
        // immutable QMOD payload. The August 2026 QMOD deliberately ships a
        // newer emergency baseline because yt-dlp 2026.07.04's Android-VR
        // client now fails public downloads with HTTP 403. Retaining that old
        // active package after a QMOD upgrade would silently bypass the fix.
        // Remove only a well-formed, provably older active package; unknown or
        // newer versions remain eligible for the existing import/rollback
        // transaction below.
        if(Utility::IsRegularFile(active))
        {
            std::ifstream versionFile(active.string() + ".version");
            std::string activeVersion;
            if(versionFile)
                std::getline(versionFile, activeVersion);
            if(VersionIsOlder(activeVersion, BundledYtDlpVersion))
            {
                std::error_code removeError;
                std::filesystem::remove(active, removeError);
                if(!removeError)
                    std::filesystem::remove(
                        active.string() + ".version", removeError);
                if(!removeError)
                    std::filesystem::remove(
                        active.string() + ".channel", removeError);
                if(removeError)
                {
                    return fail(
                        "BS-DL-INIT-114",
                        "Could not replace the obsolete yt-dlp " +
                            activeVersion + " runtime: " +
                            removeError.message());
                }
                SetCurrentYtDlpIdentity(
                    std::string(BundledYtDlpVersion), "nightly");
                PaperLogger.info(
                    "Replaced obsolete active yt-dlp {} with shipped baseline {}",
                    activeVersion,
                    BundledYtDlpVersion);
            }
        }

        // Promote only after every fallible native/certificate prerequisite
        // has passed. From this point until Accept(), Activation
        // owns restoration if bridge registration, CPython initialization, or
        // validation fails.
        Activation activation{runtime};
        if(!activation.Promote(error))
            return fail("BS-DL-INIT-105", std::move(error));
        const auto promotedCandidate = activation.Promoted();
        const auto candidateVersion = activation.CandidateVersion();
        std::ifstream activeVersion(active.string() + ".version");
        if(activeVersion)
        {
            std::string version;
            std::string channel;
            std::getline(activeVersion, version);
            std::ifstream activeChannel(active.string() + ".channel");
            if(activeChannel) std::getline(activeChannel, channel);
            SetCurrentYtDlpIdentity(
                std::move(version), std::move(channel));
        }
        else
            SetCurrentYtDlpIdentity(
                std::string(BundledYtDlpVersion), "nightly");

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
            PythonObject builtins{PyImport_ImportModule("builtins")};
            if(!globals || !builtins ||
               PyDict_SetItemString(
                   globals.get(), "__builtins__", builtins.get()) < 0)
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
                std::string restoredVersionText(BundledYtDlpVersion);
                std::string restoredChannelText = "nightly";
                std::ifstream restoredVersion(active.string() + ".version");
                if(restoredVersion)
                    std::getline(restoredVersion, restoredVersionText);
                std::ifstream restoredChannel(active.string() + ".channel");
                if(restoredChannel)
                    std::getline(restoredChannel, restoredChannelText);
                SetCurrentYtDlpIdentity(
                    std::move(restoredVersionText),
                    std::move(restoredChannelText));
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
                if(Utility::IsRegularFile(active))
                {
                    auto rejectedVersion = CurrentYtDlpVersion();
                    std::error_code fileError;
                    std::filesystem::remove(active, fileError);
                    if(!fileError)
                        std::filesystem::remove(active.string() + ".version", fileError);
                    if(!fileError)
                        std::filesystem::remove(active.string() + ".channel", fileError);
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
                    SetCurrentYtDlpIdentity(
                        std::string(BundledYtDlpVersion), "nightly");
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
        try
        {
            worker_ = std::thread([this]() { RunOperationQueue(); });
            statusWorker_ = std::thread([this]() { RunStatusPolling(); });
        }
        catch(const std::exception& exception)
        {
            {
                std::scoped_lock lock(operationMutex_);
                stopOperationWorker_ = true;
            }
            operationWake_.notify_all();
            if(worker_.joinable()) worker_.join();
            return fail(
                "BS-DL-INIT-115",
                std::string("Could not start the downloader operation worker: ") +
                    exception.what());
        }
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

    std::string DownloadManager::CurrentYtDlpVersion() const
    {
        std::scoped_lock lock(versionMutex_);
        return currentUpdateVersion_;
    }

    std::string DownloadManager::CurrentYtDlpChannel() const
    {
        std::scoped_lock lock(versionMutex_);
        return currentUpdateChannel_;
    }

    void DownloadManager::SetCurrentYtDlpIdentity(
        std::string version,
        std::string channel)
    {
        std::scoped_lock lock(versionMutex_);
        currentUpdateVersion_ = version.empty()
            ? std::string(BundledYtDlpVersion)
            : std::move(version);
        currentUpdateChannel_ = NormalizeChannel(
            std::move(channel), currentUpdateVersion_);
    }

    YtDlpReleaseSnapshot DownloadManager::YtDlpReleaseStatus() const
    {
        std::scoped_lock lock(ytDlpReleaseMutex_);
        return ytDlpReleaseSnapshot_;
    }

    ModReleaseSnapshot DownloadManager::ModReleaseStatus() const
    {
        std::scoped_lock lock(modReleaseMutex_);
        return modReleaseSnapshot_;
    }

    std::optional<std::string> DownloadManager::TakeUpdateNotice()
    {
        std::scoped_lock lock(mutex_);
        auto result = updateNotice_;
        updateNotice_.reset();
        return result;
    }

    std::optional<ModReleaseNotice> DownloadManager::TakeModReleaseNotice()
    {
        std::scoped_lock lock(modReleaseMutex_);
        auto result = modReleaseNotice_;
        modReleaseNotice_.reset();
        return result;
    }

    std::optional<YtDlpReleaseNotice>
    DownloadManager::TakeYtDlpReleaseNotice()
    {
        std::scoped_lock lock(ytDlpReleaseMutex_);
        auto result = ytDlpReleaseNotice_;
        ytDlpReleaseNotice_.reset();
        return result;
    }

    void DownloadManager::RecordYouTubeDownloadOutcome(DownloadState state)
    {
        bool startReleaseCheck = false;
        {
            std::scoped_lock lock(ytDlpReleaseMutex_);
            if(state == DownloadState::Completed)
            {
                consecutiveYoutubeDownloadFailures_ = 0;
                youtubeFailureGuidanceShown_ = false;
                youtubeFailureGuidancePending_ = false;
                return;
            }
            if(state != DownloadState::Failed)
                return;

            ++consecutiveYoutubeDownloadFailures_;
            if(consecutiveYoutubeDownloadFailures_ < 3 ||
               youtubeFailureGuidanceShown_)
                return;

            youtubeFailureGuidanceShown_ = true;
            youtubeFailureGuidancePending_ = true;
            startReleaseCheck = true;
        }

        if(startReleaseCheck)
        {
            std::string error;
            const bool installedNightly = CurrentYtDlpChannel() == "nightly";
            if(!StartYtDlpReleaseCheck(installedNightly, true, error) &&
               !YtDlpReleaseStatus().Active())
            {
                // A check already in flight will consume the pending guidance
                // when it completes. Only fall back immediately when no check
                // can run at all.
                std::scoped_lock lock(ytDlpReleaseMutex_);
                youtubeFailureGuidancePending_ = false;
                if(!ytDlpReleaseNotice_)
                {
                    ytDlpReleaseNotice_ = YtDlpReleaseNotice{
                        "Several YouTube downloads failed",
                        "This may not be a problem with Big Screen or the video address. YouTube sometimes changes how videos are delivered. Open the Update tab and check yt-dlp for updates. If stable is current and downloads still fail, a nightly release may contain an early compatibility fix."};
                }
                PaperLogger.info(
                    "Could not run the post-failure yt-dlp check: {}", error);
            }
        }
    }

    bool DownloadManager::Start(DownloadRequest request, std::string& error)
    {
        std::scoped_lock startLock(startMutex_);
        std::vector<int> verifiedAvailableHeights;
        if(!initialized_)
        {
            error = UnavailableMessage();
            return false;
        }
        if(operationBusy_)
        {
            error = "Another video is already downloading.";
            return false;
        }
        {
            std::scoped_lock lock(mutex_);
            if(snapshot_.Active())
            {
                error = "Another video is already downloading.";
                return false;
            }
            // A transfer is started from a successful metadata probe. Preserve
            // that verified tier list while the snapshot changes from probe
            // state to transfer state so the UI can restore the same explicit
            // resolution choices after a successful replacement/download.
            if(snapshot_.metadataOnly &&
               snapshot_.state == DownloadState::ProbeCompleted &&
               snapshot_.levelId == request.levelId)
                verifiedAvailableHeights = snapshot_.availableHeights;
        }
        if(request.levelId.empty() || request.sourceUrl.empty())
        {
            error = "A song and YouTube URL are required.";
            return false;
        }
        if(request.requestedHeight < 1 || request.requestedHeight > 1440)
        {
            error = "Select an available video resolution before downloading.";
            return false;
        }
        if(!CoreLogic::IsSupportedYouTubeUrl(request.sourceUrl))
        {
            error = "Only HTTPS YouTube and youtu.be video addresses are supported.";
            return false;
        }
        auto& library = VideoLibrary::Instance();
        const auto finalPath = library.AllocateVideoPath(
            request.levelId,
            request.origin,
            request.requestedHeight == 1440 ? ".webm" : ".mp4");
        const auto statusPath = library.RuntimePath() / "download-status.json";
        const auto cancelPath = library.RuntimePath() / "download.cancel";
        const auto downloaderDiagnosticPath =
            library.RuntimePath() / "download-ytdlp.jsonl";
        std::filesystem::remove(cancelPath);
        std::filesystem::remove(statusPath);
        std::filesystem::remove(downloaderDiagnosticPath);
        {
            std::scoped_lock lock(mutex_);
            statusPath_ = statusPath;
            cancelPath_ = cancelPath;
            downloaderDiagnosticPath_ = downloaderDiagnosticPath;
            downloaderDiagnosticOffset_ = 0;
            ignoreStatusFile_ = false;
            snapshot_ = {};
            snapshot_.state = DownloadState::Preparing;
            snapshot_.levelId = request.levelId;
            snapshot_.message = "Checking video information";
            snapshot_.requestedHeight = request.requestedHeight;
            snapshot_.availableHeights = std::move(verifiedAvailableHeights);
        }
        {
            // PollStatusFile owns these throttle fields under this mutex. A
            // previous operation's final status poll may still be completing
            // while the next Start call initializes its state; serialize the
            // reset so the std::string cannot receive concurrent writes.
            std::scoped_lock statusReadLock(statusReadMutex_);
            lastDiagnosticState_ = DownloadState::Idle;
            lastDiagnosticProgressBucket_ = -1;
            lastUnknownSizeDiagnostic_ = {};
            lastDownloaderDiagnostic_.clear();
        }
        PaperLogger.info(
            "Starting video download for '{}' ({})",
            request.songName,
            request.levelId);
        if(!QueueOperation(
            [this, request = std::move(request), finalPath]() mutable {
                Run(std::move(request), finalPath);
            },
            error))
        {
            SetFailure(error);
            return false;
        }
        return true;
    }

    bool DownloadManager::StartMapPackage(
        MapPackageRequest request,
        std::string& error)
    {
        std::scoped_lock startLock(startMutex_);
        if(!initialized_)
        {
            error = UnavailableMessage();
            return false;
        }
        if(operationBusy_)
        {
            error = "Another downloader task is already running.";
            return false;
        }
        {
            std::scoped_lock lock(mutex_);
            if(snapshot_.Active())
            {
                error = "Another downloader task is already running.";
                return false;
            }
        }
        if(request.mapKey.empty() || request.expectedHash.size() != 40 ||
           request.destinationDirectory.empty())
        {
            error = "The showcase map identity is incomplete.";
            return false;
        }

        // MapPackageScript is intentionally restricted to a direct child of
        // Big Screen/DemoLevels. This makes replacement of a stale managed
        // install safe and prevents a future caller from turning ZIP install
        // cleanup into deletion of an arbitrary Quest directory.
        const auto demoRoot =
            (VideoLibrary::Instance().RootPath() / "DemoLevels").lexically_normal();
        const auto destination = request.destinationDirectory.lexically_normal();
        if(destination.parent_path() != demoRoot ||
           destination.filename().empty())
        {
            error = "The showcase map destination is outside Big Screen's managed demo folder.";
            return false;
        }

        const auto runtime = VideoLibrary::Instance().RuntimePath();
        const auto statusPath = runtime / "showcase-map-status.json";
        const auto cancelPath = runtime / "showcase-map.cancel";
        std::filesystem::remove(statusPath);
        std::filesystem::remove(cancelPath);
        {
            std::scoped_lock lock(mutex_);
            statusPath_ = statusPath;
            cancelPath_ = cancelPath;
            ignoreStatusFile_ = false;
            snapshot_ = {};
            snapshot_.state = DownloadState::Preparing;
            snapshot_.levelId = "__showcase_map__";
            snapshot_.message = "Finding exact BeatSaver map revision";
        }
        PaperLogger.info(
            "Starting managed BeatSaver map download for key '{}' revision '{}'",
            request.mapKey,
            request.expectedHash);
        if(!QueueOperation(
            [this, request = std::move(request)]() mutable {
                RunMapPackage(std::move(request));
            },
            error))
        {
            SetFailure(error);
            return false;
        }
        return true;
    }

    bool DownloadManager::StartProbe(
        std::string levelId,
        std::string sourceUrl,
        std::string& error)
    {
        std::scoped_lock startLock(startMutex_);
        if(!initialized_)
        {
            error = UnavailableMessage();
            return false;
        }
        if(operationBusy_)
        {
            error = "Another downloader task is already running.";
            return false;
        }
        {
            std::scoped_lock lock(mutex_);
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
        const auto runtime = VideoLibrary::Instance().RuntimePath();
        const auto statusPath = runtime / "url-probe-status.json";
        const auto cancelPath = runtime / "url-probe.cancel";
        std::filesystem::remove(statusPath);
        std::filesystem::remove(cancelPath);
        {
            std::scoped_lock lock(mutex_);
            statusPath_ = statusPath;
            cancelPath_ = cancelPath;
            ignoreStatusFile_ = false;
            snapshot_ = {};
            snapshot_.state = DownloadState::Probing;
            snapshot_.levelId = levelId;
            snapshot_.message = "Checking YouTube URL";
            snapshot_.metadataOnly = true;
        }
        PaperLogger.info("Checking a YouTube URL for {}", levelId);
        if(!QueueOperation([
                this,
                levelId = std::move(levelId),
                sourceUrl = std::move(sourceUrl)]() mutable
            {
                RunProbe(std::move(levelId), std::move(sourceUrl));
            },
            error))
        {
            SetFailure(error);
            return false;
        }
        return true;
    }

    bool DownloadManager::StartUpdaterCheck(
        bool nightly,
        bool install,
        std::string& error,
        bool channelSwitch)
    {
        std::scoped_lock startLock(startMutex_);
        if(!initialized_) { error = UnavailableMessage(); return false; }
        if(operationBusy_) { error = "A downloader task is already running."; return false; }
        {
            std::scoped_lock lock(mutex_);
            if(snapshot_.Active()) { error = "A downloader task is already running."; return false; }
        }
        const auto runtime = VideoLibrary::Instance().RuntimePath();
        const auto statusPath = runtime / "update-status.json";
        const auto cancelPath = runtime / "update.cancel";
        std::filesystem::remove(statusPath);
        std::filesystem::remove(cancelPath);
        {
            std::scoped_lock lock(mutex_);
            statusPath_ = statusPath;
            cancelPath_ = cancelPath;
            ignoreStatusFile_ = false;
            snapshot_ = {};
            snapshot_.state = DownloadState::Preparing;
            snapshot_.levelId = "__updater__";
            snapshot_.message = "Checking yt-dlp releases";
        }
        if(!QueueOperation(
            [this, nightly, install, channelSwitch]() {
                RunUpdater(nightly, install, channelSwitch);
            },
            error))
        {
            SetFailure(error);
            return false;
        }
        return true;
    }

    void DownloadManager::StartAutomaticYtDlpReleaseCheck()
    {
        bool expected = false;
        if(!automaticYtDlpReleaseCheckStarted_.compare_exchange_strong(
               expected, true))
            return;

        // Follow the package that is actually loaded, not merely the user's
        // preference toggle. A nightly install checks stable first in the
        // Python release policy and falls back to nightly only when stable has
        // not caught up. A stable install is never auto-promoted to nightly.
        std::string error;
        const bool installedNightly = CurrentYtDlpChannel() == "nightly";
        if(!StartYtDlpReleaseCheck(installedNightly, true, error))
            PaperLogger.info(
                "Automatic yt-dlp release check was not started: {}", error);
    }

    bool DownloadManager::StartYtDlpReleaseCheck(
        bool requestedNightly,
        std::string& error)
    {
        return StartYtDlpReleaseCheck(requestedNightly, false, error);
    }

    bool DownloadManager::StartYtDlpReleaseCheck(
        bool requestedNightly,
        bool automatic,
        std::string& error)
    {
        std::scoped_lock startLock(ytDlpReleaseStartMutex_);
        if(!initialized_)
        {
            error = UnavailableMessage();
            std::scoped_lock lock(ytDlpReleaseMutex_);
            ytDlpReleaseSnapshot_.state =
                YtDlpReleaseCheckState::Unavailable;
            ytDlpReleaseSnapshot_.currentVersion = CurrentYtDlpVersion();
            ytDlpReleaseSnapshot_.currentChannel = CurrentYtDlpChannel();
            ytDlpReleaseSnapshot_.message =
                "yt-dlp update checking is unavailable because the embedded network runtime did not initialize.";
            if(!automatic)
            {
                ytDlpReleaseNotice_ = YtDlpReleaseNotice{
                    "Could not check yt-dlp",
                    ytDlpReleaseSnapshot_.message + "\n\n" + error};
            }
            return false;
        }
        {
            std::scoped_lock lock(ytDlpReleaseMutex_);
            if(ytDlpReleaseSnapshot_.Active())
            {
                error = "A yt-dlp release check is already running.";
                return false;
            }
        }
        if(ytDlpReleaseWorker_.joinable())
        {
            if(!ytDlpReleaseWorkerFinished_)
            {
                error = "A yt-dlp release check is still finishing.";
                return false;
            }
            // Never join a completed network worker from a Unity callback.
            ytDlpReleaseWorker_.detach();
        }
        {
            std::scoped_lock lock(ytDlpReleaseMutex_);
            ytDlpReleaseSnapshot_ = {};
            ytDlpReleaseSnapshot_.state =
                YtDlpReleaseCheckState::Checking;
            ytDlpReleaseSnapshot_.currentVersion = CurrentYtDlpVersion();
            ytDlpReleaseSnapshot_.currentChannel = CurrentYtDlpChannel();
            ytDlpReleaseSnapshot_.checkedChannel =
                requestedNightly ? "nightly" : "stable";
            ytDlpReleaseSnapshot_.message =
                "Checking yt-dlp releases in the background...";
        }
        try
        {
            ytDlpReleaseWorkerFinished_ = false;
            ytDlpReleaseWorker_ = std::thread(
                [this, automatic, requestedNightly]() {
                    try
                    {
                        RunYtDlpReleaseCheck(automatic, requestedNightly);
                    }
                    catch(const std::exception& exception)
                    {
                        {
                            std::scoped_lock lock(ytDlpReleaseMutex_);
                            ytDlpReleaseSnapshot_.state =
                                YtDlpReleaseCheckState::Unavailable;
                            ytDlpReleaseSnapshot_.message =
                                "The yt-dlp update check stopped unexpectedly.";
                            if(!automatic)
                            {
                                ytDlpReleaseNotice_ = YtDlpReleaseNotice{
                                    "Could not check yt-dlp",
                                    ytDlpReleaseSnapshot_.message};
                            }
                        }
                        ErrorManager::Instance().RecordError(
                            "Checking yt-dlp releases", exception.what());
                    }
                    catch(...)
                    {
                        {
                            std::scoped_lock lock(ytDlpReleaseMutex_);
                            ytDlpReleaseSnapshot_.state =
                                YtDlpReleaseCheckState::Unavailable;
                            ytDlpReleaseSnapshot_.message =
                                "The yt-dlp update check stopped unexpectedly.";
                            if(!automatic)
                            {
                                ytDlpReleaseNotice_ = YtDlpReleaseNotice{
                                    "Could not check yt-dlp",
                                    ytDlpReleaseSnapshot_.message};
                            }
                        }
                        ErrorManager::Instance().RecordError(
                            "Checking yt-dlp releases",
                            "Unexpected release-check worker failure");
                    }
                    ytDlpReleaseWorkerFinished_ = true;
                });
        }
        catch(const std::exception& exception)
        {
            ytDlpReleaseWorkerFinished_ = true;
            error = std::string(
                "Could not start the yt-dlp release-check worker: ") +
                exception.what();
            std::scoped_lock lock(ytDlpReleaseMutex_);
            ytDlpReleaseSnapshot_.state =
                YtDlpReleaseCheckState::Unavailable;
            ytDlpReleaseSnapshot_.message =
                "Big Screen could not start the yt-dlp update check.";
            if(!automatic)
            {
                ytDlpReleaseNotice_ = YtDlpReleaseNotice{
                    "Could not check yt-dlp",
                    ytDlpReleaseSnapshot_.message};
            }
            ErrorManager::Instance().RecordError(
                "Starting yt-dlp release check", exception.what());
            return false;
        }
        return true;
    }

    void DownloadManager::StartAutomaticModReleaseCheck()
    {
        bool expected = false;
        if(!automaticModReleaseCheckStarted_.compare_exchange_strong(
               expected, true))
            return;

        std::string error;
        if(!StartModReleaseCheck(true, error))
            PaperLogger.info(
                "Automatic Big Screen release check was not started: {}",
                error);
    }

    bool DownloadManager::StartModReleaseCheck(std::string& error)
    {
        return StartModReleaseCheck(false, error);
    }

    bool DownloadManager::StartModReleaseCheck(
        bool automatic,
        std::string& error)
    {
        std::scoped_lock startLock(modReleaseStartMutex_);
        if(!initialized_)
        {
            error = UnavailableMessage();
            std::scoped_lock lock(modReleaseMutex_);
            modReleaseSnapshot_ = {
                ModReleaseCheckState::Unavailable,
                VERSION,
                {},
                "Release checking is unavailable because the embedded network runtime did not initialize."};
            if(!automatic)
            {
                modReleaseNotice_ = ModReleaseNotice{
                    "Could not check for updates",
                    modReleaseSnapshot_.message +
                        "\n\n" + error};
            }
            return false;
        }
        {
            std::scoped_lock lock(modReleaseMutex_);
            if(modReleaseSnapshot_.Active())
            {
                error = "A Big Screen release check is already running.";
                return false;
            }
        }
        if(modReleaseWorker_.joinable())
        {
            if(!modReleaseWorkerFinished_)
            {
                error = "A Big Screen release check is still finishing.";
                return false;
            }
            // The completion flag is the worker's final shared-state action.
            // Release its completed thread handle instead of joining from the
            // Check Update button callback on Unity's main thread.
            modReleaseWorker_.detach();
        }
        {
            std::scoped_lock lock(modReleaseMutex_);
            modReleaseSnapshot_ = {
                ModReleaseCheckState::Checking,
                VERSION,
                {},
                "Checking GitHub for the latest Big Screen release..."};
        }
        try
        {
            modReleaseWorkerFinished_ = false;
            modReleaseWorker_ = std::thread(
                [this, automatic]() {
                    try
                    {
                        RunModReleaseCheck(automatic);
                    }
                    catch(const std::exception& exception)
                    {
                        {
                            std::scoped_lock lock(modReleaseMutex_);
                            modReleaseSnapshot_.state =
                                ModReleaseCheckState::Unavailable;
                            modReleaseSnapshot_.message =
                                "The update check stopped unexpectedly.";
                        }
                        ErrorManager::Instance().RecordError(
                            "Checking for a Big Screen release",
                            exception.what());
                    }
                    catch(...)
                    {
                        {
                            std::scoped_lock lock(modReleaseMutex_);
                            modReleaseSnapshot_.state =
                                ModReleaseCheckState::Unavailable;
                            modReleaseSnapshot_.message =
                                "The update check stopped unexpectedly.";
                        }
                        ErrorManager::Instance().RecordError(
                            "Checking for a Big Screen release",
                            "Unexpected release-check worker failure");
                    }
                    modReleaseWorkerFinished_ = true;
                });
        }
        catch(const std::exception& exception)
        {
            modReleaseWorkerFinished_ = true;
            error = std::string("Could not start the release-check worker: ") +
                exception.what();
            std::scoped_lock lock(modReleaseMutex_);
            modReleaseSnapshot_.state = ModReleaseCheckState::Unavailable;
            modReleaseSnapshot_.message =
                "Big Screen could not start its release check.";
            if(!automatic)
            {
                modReleaseNotice_ = ModReleaseNotice{
                    "Could not check for updates",
                    modReleaseSnapshot_.message};
            }
            ErrorManager::Instance().RecordError(
                "Starting Big Screen release check", exception.what());
            return false;
        }
        return true;
    }

    void DownloadManager::QueueVideoThumbnail(
        std::string levelId,
        std::string sourceUrl,
        std::filesystem::path destination)
    {
        if(!initialized_ || levelId.empty() || sourceUrl.empty() ||
           destination.empty() || Utility::IsRegularFile(destination))
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
            {
                try
                {
                    thumbnailWorker_ =
                        std::thread([this]() { RunThumbnailQueue(); });
                }
                catch(const std::exception& exception)
                {
                    thumbnailQueue_.pop_back();
                    requestedThumbnails_.erase(key);
                    ErrorManager::Instance().RecordError(
                        "Starting the video thumbnail worker",
                        exception.what());
                    return;
                }
            }
        }
        thumbnailWake_.notify_one();
    }

    bool DownloadManager::QueueOperation(
        std::function<void()> operation,
        std::string& error)
    {
        if(!operation)
        {
            error = "The downloader operation was empty.";
            return false;
        }
        if(operationBusy_.exchange(true))
        {
            error = "Another downloader task is already running.";
            return false;
        }
        try
        {
            std::scoped_lock lock(operationMutex_);
            if(stopOperationWorker_)
            {
                operationBusy_ = false;
                error = "The downloader is shutting down.";
                return false;
            }
            pendingOperation_ = std::move(operation);
        }
        catch(const std::exception& exception)
        {
            operationBusy_ = false;
            error = std::string("Could not queue the downloader task: ") +
                exception.what();
            return false;
        }
        operationWake_.notify_one();
        return true;
    }

    void DownloadManager::RunOperationQueue()
    {
        pthread_setname_np(pthread_self(), "BigScreenDL");
        while(true)
        {
            std::function<void()> operation;
            {
                std::unique_lock lock(operationMutex_);
                operationWake_.wait(lock, [this]() {
                    return stopOperationWorker_ ||
                        static_cast<bool>(pendingOperation_);
                });
                if(stopOperationWorker_ && !pendingOperation_)
                    return;
                operation = std::move(pendingOperation_);
                pendingOperation_ = {};
            }

            PaperLogger.info("Downloader background operation started");
            try
            {
                operation();
            }
            catch(const std::exception& exception)
            {
                SetFailure(
                    std::string("Downloader stopped: ") + exception.what());
            }
            catch(...)
            {
                SetFailure(
                    "Downloader stopped because of an unexpected internal error.");
            }
            // Run()/RunProbe()/RunUpdater publish their terminal snapshots
            // before returning. Clear busy last so a new UI action can never
            // overlap final file promotion or manifest persistence.
            operationBusy_ = false;
            PaperLogger.info("Downloader background operation finished");
        }
    }

    void DownloadManager::RunStatusPolling()
    {
        pthread_setname_np(pthread_self(), "BigScreenDLStat");
        while(true)
        {
            {
                std::unique_lock lock(statusWaitMutex_);
                if(statusWake_.wait_for(
                       lock,
                       std::chrono::milliseconds(100),
                       [this]() { return stopStatusWorker_; }))
                    return;
            }
            if(operationBusy_)
                RefreshSnapshotFromDisk();
        }
    }

    void DownloadManager::Cancel()
    {
        std::filesystem::path cancelPath;
        std::string levelId;
        {
            std::scoped_lock lock(mutex_);
            if(!snapshot_.Active()) return;
            cancelPath = cancelPath_;
            levelId = snapshot_.levelId;
            snapshot_.message = "Stopping download";
        }
        DiagnosticSessionLogger::Instance().DownloadEvent(
            "download_cancel_requested", "DownloadManager", {
                {"levelId", levelId}});
        std::ofstream stream(cancelPath, std::ios::binary | std::ios::trunc);
        stream << "cancel";
        stream.flush();
        if(!stream)
        {
            {
                std::scoped_lock lock(mutex_);
                if(cancelPath_ == cancelPath)
                    snapshot_.message =
                        "Could not request downloader cancellation.";
            }
            PaperLogger.error(
                "Could not write downloader cancellation marker '{}'.",
                cancelPath.string());
            ErrorManager::Instance().RecordError(
                "Cancelling downloader operation",
                "The cancellation marker could not be written to " +
                    cancelPath.string());
            return;
        }
        PaperLogger.info("Downloader cancellation requested for {}", levelId);
    }

    DownloadSnapshot DownloadManager::Snapshot()
    {
        std::scoped_lock lock(mutex_);
        // Status-file I/O is owned by RunStatusPolling. Unity only copies this
        // in-memory mailbox and can never stall on shared-storage reads.
        auto result = snapshot_;
        if(operationBusy_ && !result.Active())
        {
            // A status file can reach a terminal state a few milliseconds
            // before the operation thread finishes promotion, persistence, or
            // diagnostic logging. Do not let UI code start another task or
            // open the new decoder until that worker is genuinely finished.
            result.state = DownloadState::Preparing;
            result.message = "Finishing downloader task";
        }
        return result;
    }

    void DownloadManager::Run(DownloadRequest request, std::filesystem::path finalPath)
    {
        // Count one result per user-started transfer even if a later logging
        // or publication exception reaches the outer guard. Without this
        // latch, one failed attempt could advance the three-failure guidance
        // threshold twice.
        bool outcomeRecorded = false;
        try
        {
            const auto thumbnailPath =
                VideoLibrary::Instance().AllocateThumbnailPath(
                    request.levelId, request.origin);
            const auto incomingVideoPath = IncomingSibling(finalPath);
            const auto incomingThumbnailPath = IncomingSibling(thumbnailPath);
            rapidjson::Document document(rapidjson::kObjectType);
            auto& allocator = document.GetAllocator();
            AddString(document, "sourceUrl", request.sourceUrl, allocator);
            AddString(
                document, "finalPath", incomingVideoPath.string(), allocator);
            AddString(
                document,
                "thumbnailPath",
                incomingThumbnailPath.string(),
                allocator);
            AddString(document, "statusPath", statusPath_.string(), allocator);
            AddString(document, "cancelPath", cancelPath_.string(), allocator);
            AddString(
                document, "ytdlpLogPath",
                DiagnosticSessionLogger::Instance().DownloadSessionActive()
                    ? downloaderDiagnosticPath_.string()
                    : std::string{},
                allocator);
            document.AddMember("explicitContentAllowed", request.explicitContentAllowed, allocator);
            document.AddMember("requestedHeight", request.requestedHeight, allocator);
            document.AddMember("maximumSourceFps", request.maximumSourceFps, allocator);
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
            std::string pythonFailure;
            {
                ScopedPythonGil gil;
                auto globals = CreatePythonGlobals(
                    buffer.GetString(), buffer.GetSize());
                PythonObject result;
                if(globals)
                {
                    const std::string script =
                        std::string(MediaScriptHelpers) + DownloaderScript;
                    result.reset(PyRun_String(
                        script.c_str(),
                        Py_file_input,
                        globals.get(),
                        globals.get()));
                }
                if(!globals || !result)
                {
                    pythonFailure = TakePythonExceptionText();
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
                            std::filesystem::remove(
                                active.string() + ".channel", fileError);
                        if(!fileError)
                            std::filesystem::rename(previous, active, fileError);
                        if(!fileError && std::filesystem::is_regular_file(
                               previous.string() + ".version", fileError))
                            std::filesystem::rename(
                                previous.string() + ".version",
                                active.string() + ".version",
                                fileError);
                        if(!fileError && std::filesystem::is_regular_file(
                               previous.string() + ".channel", fileError))
                            std::filesystem::rename(
                                previous.string() + ".channel",
                                active.string() + ".channel",
                                fileError);
                        if(!fileError)
                        {
                            std::ofstream rejected(
                                runtime / "yt-dlp-rejected.version",
                                std::ios::binary | std::ios::trunc);
                            rejected << (rejectedVersion.empty()
                                ? "unknown"
                                : rejectedVersion);
                            std::string restoredVersionText(BundledYtDlpVersion);
                            std::string restoredChannelText = "nightly";
                            std::ifstream restoredVersion(
                                active.string() + ".version", std::ios::binary);
                            if(restoredVersion)
                                std::getline(
                                    restoredVersion, restoredVersionText);
                            std::ifstream restoredChannel(
                                active.string() + ".channel", std::ios::binary);
                            if(restoredChannel)
                                std::getline(
                                    restoredChannel, restoredChannelText);
                            SetCurrentYtDlpIdentity(
                                std::move(restoredVersionText),
                                std::move(restoredChannelText));
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
                                const auto retryFailure =
                                    TakePythonExceptionText();
                                pythonFailure +=
                                    "\nRetry after rollback:\n" + retryFailure;
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
                PaperLogger.error(
                    "Embedded downloader Python failure:\n{}",
                    pythonFailure);
                ErrorManager::Instance().RecordError(
                    "Embedded downloader Python execution",
                    pythonFailure);
                SetFailure(
                    "The embedded downloader could not start. Big Screen recorded the internal error; the map and game can continue normally.");
                RecordYouTubeDownloadOutcome(DownloadState::Failed);
                outcomeRecorded = true;
                return;
            }

            DownloadSnapshot terminalSnapshot;
            RefreshSnapshotFromDisk();
            {
                std::scoped_lock lock(mutex_);
                terminalSnapshot = snapshot_;
                // Python publishes "completed" before C++ promotes the staged
                // media and durably commits library.json. Keep the public state
                // active until every fallible publication phase is finished.
                if(terminalSnapshot.state == DownloadState::Completed)
                {
                    snapshot_.state = DownloadState::Preparing;
                    snapshot_.message = "Saving downloaded video";
                }
            }
            if(terminalSnapshot.state == DownloadState::Failed)
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
                    detail.empty() ? terminalSnapshot.message : detail);
            }
            if(terminalSnapshot.state == DownloadState::Completed)
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

                // Promote only after yt-dlp has published a complete, probed
                // file. The transactions roll back automatically if either
                // filesystem publication or the manifest update throws.
                PaperLogger.info(
                    "Publishing downloaded video file for {}",
                    request.levelId);
                StagedFileReplacement videoReplacement(
                    incomingVideoPath, finalPath);
                videoReplacement.Promote();
                std::optional<StagedFileReplacement> thumbnailReplacement;
                std::error_code thumbnailError;
                if(std::filesystem::is_regular_file(
                       incomingThumbnailPath, thumbnailError) &&
                   !thumbnailError)
                {
                    thumbnailReplacement.emplace(
                        incomingThumbnailPath, thumbnailPath);
                    thumbnailReplacement->Promote();
                }
                VideoLibrary::Instance().CommitDownload(
                    request.levelId,
                    request.songName,
                    request.songAuthor,
                    request.origin,
                    std::move(stored));
                PaperLogger.info(
                    "Committed downloaded video manifest for {}",
                    request.levelId);
                videoReplacement.Commit();
                if(thumbnailReplacement)
                    thumbnailReplacement->Commit();
                terminalSnapshot.thumbnailPath =
                    Utility::IsRegularFile(thumbnailPath)
                        ? thumbnailPath.string()
                        : std::string{};
                const auto diagnostic = ReadString(status, "diagnostic");
                if(!diagnostic.empty())
                    PaperLogger.warn(
                        "Video download completed with a non-fatal warning: {}",
                        diagnostic);
            }
            {
                std::scoped_lock lock(mutex_);
                snapshot_ = terminalSnapshot;
            }
            RecordYouTubeDownloadOutcome(terminalSnapshot.state);
            outcomeRecorded = true;
            auto& diagnostics = DiagnosticSessionLogger::Instance();
            const auto terminalState = terminalSnapshot.state;
            DiagnosticFields terminalFields{
                {"levelId", request.levelId},
                {"state", StateName(terminalState)},
                {"message", terminalSnapshot.message},
                {"downloadedBytes", std::to_string(
                    terminalSnapshot.downloadedBytes)},
                {"totalBytes", std::to_string(terminalSnapshot.totalBytes)}};
            if(terminalState == DownloadState::Completed)
            {
                terminalFields.emplace_back("outputPath", finalPath.string());
                diagnostics.DownloadEvent(
                    "download_completed", "DownloadManager", terminalFields);
                diagnostics.EndDownloadSession("completed", terminalFields);
            }
            else if(terminalState == DownloadState::Cancelled)
            {
                diagnostics.DownloadEvent(
                    "download_cancelled", "DownloadManager", terminalFields);
                diagnostics.EndDownloadSession("cancelled", terminalFields);
            }
            else if(terminalState == DownloadState::Failed)
            {
                terminalFields.emplace_back("errorCode", terminalSnapshot.errorCode);
                diagnostics.DownloadEvent(
                    "download_failed", "DownloadManager", terminalFields);
                diagnostics.EndDownloadSession("failed", terminalFields);
            }
            PaperLogger.info(
                "Downloader finished for {} with state '{}': {}",
                request.levelId,
                StateName(terminalSnapshot.state),
                terminalSnapshot.message);
        }
        catch(const std::exception& exception)
        {
            SetFailure(std::string("Downloader stopped: ") + exception.what());
            if(!outcomeRecorded)
                RecordYouTubeDownloadOutcome(DownloadState::Failed);
        }
        catch(...)
        {
            SetFailure("Downloader stopped because of an unexpected internal error.");
            if(!outcomeRecorded)
                RecordYouTubeDownloadOutcome(DownloadState::Failed);
        }
    }

    void DownloadManager::RunMapPackage(MapPackageRequest request)
    {
        try
        {
            const auto runtime = VideoLibrary::Instance().RuntimePath();
            rapidjson::Document document(rapidjson::kObjectType);
            auto& allocator = document.GetAllocator();
            AddString(document, "mapKey", request.mapKey, allocator);
            AddString(document, "expectedHash", request.expectedHash, allocator);
            AddString(
                document,
                "destinationDirectory",
                request.destinationDirectory.string(),
                allocator);
            AddString(
                document,
                "archivePath",
                (runtime / "showcase-map.zip.part").string(),
                allocator);
            AddString(
                document,
                "stagingPath",
                (runtime / "showcase-map.installing").string(),
                allocator);
            AddString(document, "statusPath", statusPath_.string(), allocator);
            AddString(document, "cancelPath", cancelPath_.string(), allocator);
            AddString(document, "modVersion", VERSION, allocator);
            document.AddMember(
                "requiredFreeBytes",
                static_cast<std::uint64_t>(RequiredReserve),
                allocator);
            document.AddMember(
                "maximumArchiveBytes",
                static_cast<std::uint64_t>(16ull * 1024ull * 1024ull),
                allocator);
            document.AddMember("maximumEntries", 256, allocator);
            document.AddMember(
                "maximumExpandedBytes",
                static_cast<std::uint64_t>(96ull * 1024ull * 1024ull),
                allocator);
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            document.Accept(writer);

            bool runtimeFailed = false;
            std::string pythonFailure;
            {
                ScopedPythonGil gil;
                auto globals = CreatePythonGlobals(
                    buffer.GetString(), buffer.GetSize());
                PythonObject result;
                if(globals)
                    result.reset(PyRun_String(
                        MapPackageScript,
                        Py_file_input,
                        globals.get(),
                        globals.get()));
                if(!globals || !result)
                {
                    pythonFailure = TakePythonExceptionText();
                    runtimeFailed = true;
                }
            }
            if(runtimeFailed)
            {
                PaperLogger.error(
                    "Embedded showcase-map Python failure:\n{}",
                    pythonFailure);
                ErrorManager::Instance().RecordError(
                    "Installing the showcase map",
                    pythonFailure);
                SetFailure(
                    "The embedded downloader could not install the showcase map (BS-DEMO-MAP-002). See Big Screen's error log for details.");
                return;
            }

            RefreshSnapshotFromDisk();
            DownloadSnapshot terminalSnapshot;
            std::filesystem::path terminalStatusPath;
            {
                std::scoped_lock lock(mutex_);
                terminalSnapshot = snapshot_;
                terminalStatusPath = statusPath_;
            }
            if(terminalSnapshot.state == DownloadState::Failed)
            {
                std::ifstream diagnosticStream(
                    terminalStatusPath, std::ios::binary);
                const std::string diagnosticJson{
                    std::istreambuf_iterator<char>(diagnosticStream), {}};
                rapidjson::Document diagnosticStatus;
                diagnosticStatus.Parse(
                    diagnosticJson.data(), diagnosticJson.size());
                const auto code = ReadString(diagnosticStatus, "errorCode");
                const auto detail = ReadString(diagnosticStatus, "diagnostic");
                ErrorManager::Instance().RecordError(
                    code.empty() ? "Showcase map download" : code,
                    detail.empty() ? terminalSnapshot.message : detail);
            }
            PaperLogger.info(
                "Showcase map downloader finished with state '{}': {}",
                StateName(terminalSnapshot.state),
                terminalSnapshot.message);
        }
        catch(const std::exception& exception)
        {
            SetFailure(
                std::string("Showcase map installer stopped: ") +
                exception.what());
        }
        catch(...)
        {
            SetFailure(
                "Showcase map installer stopped because of an unexpected internal error.");
        }
    }

    void DownloadManager::RunProbe(std::string levelId, std::string sourceUrl)
    {
        bool failureRecorded = false;
        try
        {
            const auto thumbnailPath =
                VideoLibrary::Instance().RuntimePath() / "url-thumbnail.jpg";
            rapidjson::Document document(rapidjson::kObjectType);
            auto& allocator = document.GetAllocator();
            AddString(document, "sourceUrl", sourceUrl, allocator);
            AddString(document, "statusPath", statusPath_.string(), allocator);
            AddString(document, "thumbnailPath", thumbnailPath.string(), allocator);
            AddString(document, "cancelPath", cancelPath_.string(), allocator);
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            document.Accept(writer);

            bool runtimeFailed = false;
            std::string pythonFailure;
            {
                ScopedPythonGil gil;
                auto globals = CreatePythonGlobals(
                    buffer.GetString(), buffer.GetSize());
                PythonObject result;
                if(globals)
                {
                    const std::string script =
                        std::string(MediaScriptHelpers) + ProbeScript;
                    result.reset(PyRun_String(
                        script.c_str(), Py_file_input, globals.get(), globals.get()));
                }
                if(!globals || !result)
                {
                    pythonFailure = TakePythonExceptionText();
                    runtimeFailed = true;
                }
            }
            if(runtimeFailed)
            {
                PaperLogger.error(
                    "Embedded URL probe Python failure:\n{}",
                    pythonFailure);
                ErrorManager::Instance().RecordError(
                    "Embedded URL probe Python execution",
                    pythonFailure);
                SetFailure(
                    "The embedded downloader could not check this YouTube URL. Big Screen recorded the internal error; try again after restarting Beat Saber.");
                RecordYouTubeDownloadOutcome(DownloadState::Failed);
                failureRecorded = true;
                return;
            }

            RefreshSnapshotFromDisk();
            DownloadSnapshot terminalSnapshot;
            {
                std::scoped_lock lock(mutex_);
                terminalSnapshot = snapshot_;
            }
            if(terminalSnapshot.state == DownloadState::Failed)
            {
                RecordYouTubeDownloadOutcome(DownloadState::Failed);
                failureRecorded = true;
                ErrorManager::Instance().RecordError(
                    "Checking a YouTube URL",
                    (terminalSnapshot.errorCode.empty()
                        ? std::string("BS-DL-PROBE-001")
                        : terminalSnapshot.errorCode) + ": " +
                    (terminalSnapshot.diagnostic.empty()
                        ? terminalSnapshot.message
                        : terminalSnapshot.diagnostic));
                DiagnosticSessionLogger::Instance().DownloadEvent(
                    "download_failed", "DownloadManager", {
                        {"stage", "resolution_probe"},
                        {"errorCode", terminalSnapshot.errorCode},
                        {"message", terminalSnapshot.message}});
                DiagnosticSessionLogger::Instance().EndDownloadSession(
                    "probe_failed");
            }
            else if(terminalSnapshot.state == DownloadState::Cancelled)
            {
                DiagnosticSessionLogger::Instance().DownloadEvent(
                    "resolution_cancelled", "DownloadManager", {
                        {"stage", "resolution_probe"}});
                DiagnosticSessionLogger::Instance().EndDownloadSession(
                    "cancelled_before_transfer");
            }
            PaperLogger.info(
                "URL check finished for {} with state '{}': {}",
                levelId,
                StateName(terminalSnapshot.state),
                terminalSnapshot.message);
        }
        catch(const std::exception& exception)
        {
            SetFailure(std::string("URL check stopped: ") + exception.what());
            if(!failureRecorded)
                RecordYouTubeDownloadOutcome(DownloadState::Failed);
        }
        catch(...)
        {
            SetFailure("URL check stopped because of an unexpected internal error.");
            if(!failureRecorded)
                RecordYouTubeDownloadOutcome(DownloadState::Failed);
        }
    }

    void DownloadManager::RunUpdater(
        bool nightly,
        bool install,
        bool channelSwitch)
    {
        try
        {
            const auto runtime = VideoLibrary::Instance().RuntimePath();
            rapidjson::Document document(rapidjson::kObjectType);
            auto& allocator = document.GetAllocator();
            AddString(document, "statusPath", statusPath_.string(), allocator);
            AddString(document, "cancelPath", cancelPath_.string(), allocator);
            AddString(document, "nextPath", (runtime / "yt-dlp-next").string(), allocator);
            AddString(
                document,
                "currentVersion",
                CurrentYtDlpVersion(),
                allocator);
            std::string rejectedVersion;
            std::ifstream rejected(runtime / "yt-dlp-rejected.version", std::ios::binary);
            if(rejected)
                std::getline(rejected, rejectedVersion);
            AddString(document, "rejectedVersion", rejectedVersion, allocator);
            document.AddMember("nightly", nightly, allocator);
            document.AddMember("install", install, allocator);
            document.AddMember("channelSwitch", channelSwitch, allocator);
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            document.Accept(writer);
            bool runtimeFailed = false;
            std::string pythonFailure;
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
                    pythonFailure = TakePythonExceptionText();
                    runtimeFailed = true;
                }
            }
            if(runtimeFailed)
            {
                PaperLogger.error(
                    "Embedded updater Python failure:\n{}",
                    pythonFailure);
                ErrorManager::Instance().RecordError(
                    "Embedded updater Python execution",
                    pythonFailure);
                SetFailure(
                    "The embedded downloader could not run the yt-dlp update check. Big Screen recorded the internal error; try again after restarting Beat Saber.");
                return;
            }
            RefreshSnapshotFromDisk();
            DownloadSnapshot terminal;
            {
                std::scoped_lock lock(mutex_);
                terminal = snapshot_;
            }
            if(terminal.state == DownloadState::Completed)
            {
                std::scoped_lock lock(ytDlpReleaseMutex_);
                ytDlpReleaseNotice_ = YtDlpReleaseNotice{
                    "yt-dlp update ready",
                    std::string("yt-dlp ") +
                        (nightly ? "nightly" : "stable") +
                        " was downloaded and verified. Restart Beat Saber to activate it."};
            }
            else if(terminal.state == DownloadState::UpToDate)
            {
                std::scoped_lock lock(ytDlpReleaseMutex_);
                ytDlpReleaseNotice_ = YtDlpReleaseNotice{
                    "yt-dlp is current",
                    terminal.message.empty()
                        ? "The selected yt-dlp channel is already current."
                        : terminal.message};
            }
            else if(terminal.state == DownloadState::Failed)
            {
                std::scoped_lock lock(ytDlpReleaseMutex_);
                ytDlpReleaseNotice_ = YtDlpReleaseNotice{
                    "yt-dlp update failed",
                    terminal.message.empty()
                        ? "Big Screen could not download or verify the yt-dlp update."
                        : terminal.message};
            }
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

    void DownloadManager::RunYtDlpReleaseCheck(
        bool automatic,
        bool requestedNightly)
    {
        YtDlpReleaseSnapshot outcome;
        outcome.currentVersion = CurrentYtDlpVersion();
        outcome.currentChannel = CurrentYtDlpChannel();
        outcome.state = YtDlpReleaseCheckState::Unavailable;
        outcome.message = "The yt-dlp release check did not complete.";
        try
        {
            rapidjson::Document job(rapidjson::kObjectType);
            auto& allocator = job.GetAllocator();
            AddString(
                job, "currentVersion", outcome.currentVersion, allocator);
            AddString(
                job, "currentChannel", outcome.currentChannel, allocator);
            job.AddMember("requestedNightly", requestedNightly, allocator);
            job.AddMember("automatic", automatic, allocator);
            rapidjson::StringBuffer jobBuffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(jobBuffer);
            job.Accept(writer);

            std::string resultJson;
            std::string pythonFailure;
            {
                ScopedPythonGil gil;
                auto globals = CreatePythonGlobals(
                    jobBuffer.GetString(), jobBuffer.GetSize());
                PythonObject result;
                if(globals)
                    result.reset(PyRun_String(
                        YtDlpReleaseScript,
                        Py_file_input,
                        globals.get(),
                        globals.get()));
                if(!globals || !result)
                    pythonFailure = TakePythonExceptionText();
                else if(auto* value = PyDict_GetItemString(
                            globals.get(), "BIGSCREEN_YTDLP_RELEASE_RESULT"))
                {
                    if(const char* text = PyUnicode_AsUTF8(value))
                        resultJson = text;
                    else
                        pythonFailure = TakePythonExceptionText();
                }
                else
                    pythonFailure =
                        "The yt-dlp release-check script returned no result.";
            }
            if(!pythonFailure.empty())
            {
                PaperLogger.error(
                    "Embedded yt-dlp release-check failure:\n{}",
                    pythonFailure);
                ErrorManager::Instance().RecordError(
                    "Checking yt-dlp releases", pythonFailure);
                outcome.message =
                    "Big Screen could not run the yt-dlp update check. Details were written to the error log.";
            }
            else
            {
                rapidjson::Document result;
                result.Parse(resultJson.data(), resultJson.size());
                if(result.HasParseError() || !result.IsObject())
                    throw std::runtime_error(
                        "The yt-dlp release response could not be interpreted.");

                const auto state = ReadString(result, "state");
                outcome.message = ReadString(result, "message");
                outcome.checkedChannel =
                    ReadString(result, "checkedChannel");
                outcome.availableVersion =
                    ReadString(result, "availableVersion");
                outcome.latestStableVersion =
                    ReadString(result, "stableVersion");
                outcome.latestNightlyVersion =
                    ReadString(result, "nightlyVersion");
                outcome.stableReturn =
                    result.HasMember("stableReturn") &&
                    result["stableReturn"].IsBool() &&
                    result["stableReturn"].GetBool();
                outcome.stableCaughtUp =
                    result.HasMember("stableCaughtUp") &&
                    result["stableCaughtUp"].IsBool() &&
                    result["stableCaughtUp"].GetBool();
                if(state == "update_available")
                    outcome.state =
                        YtDlpReleaseCheckState::UpdateAvailable;
                else if(state == "up_to_date")
                    outcome.state = YtDlpReleaseCheckState::UpToDate;
                else
                    outcome.state = YtDlpReleaseCheckState::Unavailable;
            }
        }
        catch(const std::exception& exception)
        {
            outcome.message =
                "Could not check yt-dlp releases. Try again later.";
            PaperLogger.error(
                "yt-dlp release check stopped: {}", exception.what());
            ErrorManager::Instance().RecordError(
                "Checking yt-dlp releases", exception.what());
        }

        // Publish the result and consume any failure-guidance request under
        // one lock. Otherwise a third failed download could set the request
        // after this worker sampled it but before the snapshot stopped being
        // "Checking", leaving no worker responsible for the popup.
        std::scoped_lock releaseLock(ytDlpReleaseMutex_);
        const bool downloadFailureGuidance =
            youtubeFailureGuidancePending_;
        youtubeFailureGuidancePending_ = false;
        std::optional<YtDlpReleaseNotice> notice;
        if(outcome.state == YtDlpReleaseCheckState::UpdateAvailable)
        {
            const bool installNightly =
                outcome.checkedChannel == "nightly";
            const bool switchChannel =
                outcome.currentChannel != outcome.checkedChannel;
            const std::string label = installNightly ? "nightly" : "stable";
            notice = YtDlpReleaseNotice{
                downloadFailureGuidance
                    ? "A yt-dlp update may fix the downloads"
                    : outcome.stableReturn
                        ? outcome.stableCaughtUp
                            ? "Stable yt-dlp has caught up"
                            : "Stable yt-dlp is older"
                        : "yt-dlp update available",
                "Installed: " + outcome.currentVersion + " (" +
                    outcome.currentChannel + ")\nAvailable: " +
                    outcome.availableVersion + " (" + label + ")\n\n" +
                    (downloadFailureGuidance
                        ? "Several YouTube downloads failed. YouTube may have changed how videos are delivered, so installing this update may restore downloads. The video address or Big Screen itself may not be the cause."
                        : outcome.stableReturn
                        ? outcome.stableCaughtUp
                            ? "The stable channel is now dated at or after your installed nightly build. You can switch back to the recommended stable release."
                            : "The stable channel has not caught up with your installed nightly build. Switching now will install an older yt-dlp release and may bring back a YouTube compatibility problem fixed by nightly. You can still switch if nightly did not solve your issue or you prefer the stable channel."
                        : outcome.message),
                true,
                installNightly,
                switchChannel};
        }
        else if(downloadFailureGuidance)
        {
            notice = YtDlpReleaseNotice{
                "Several YouTube downloads failed",
                outcome.state == YtDlpReleaseCheckState::UpToDate
                    ? "Big Screen did not find a newer yt-dlp release. This may still be caused by a recent YouTube change rather than the video address or the mod. Open the Update tab to check again later. If you are using stable and failures continue, the nightly channel may receive a compatibility fix first."
                    : "Big Screen could not complete the automatic yt-dlp update check. Open the Update tab and check again. YouTube may have changed how videos are delivered; if stable is current and downloads continue to fail, a nightly release may contain an early compatibility fix."};
        }
        else if(!automatic)
        {
            notice = YtDlpReleaseNotice{
                outcome.state == YtDlpReleaseCheckState::UpToDate
                    ? "yt-dlp is current"
                    : "Could not check yt-dlp",
                outcome.message};
        }

        ytDlpReleaseSnapshot_ = std::move(outcome);
        if(notice)
            ytDlpReleaseNotice_ = std::move(notice);
    }

    void DownloadManager::RunModReleaseCheck(bool automatic)
    {
        ModReleaseSnapshot outcome;
        outcome.currentVersion = VERSION;
        outcome.state = ModReleaseCheckState::Unavailable;
        outcome.message = "The Big Screen release check did not complete.";
        try
        {
            std::string resultJson;
            std::string pythonFailure;
            {
                ScopedPythonGil gil;
                auto globals = CreatePythonGlobals("{}", 2);
                PythonObject result;
                if(globals)
                    result.reset(PyRun_String(
                        ModReleaseScript,
                        Py_file_input,
                        globals.get(),
                        globals.get()));
                if(!globals || !result)
                    pythonFailure = TakePythonExceptionText();
                else if(auto* value = PyDict_GetItemString(
                            globals.get(), "BIGSCREEN_RELEASE_RESULT"))
                {
                    if(const char* text = PyUnicode_AsUTF8(value))
                        resultJson = text;
                    else
                        pythonFailure = TakePythonExceptionText();
                }
                else
                    pythonFailure =
                        "The release-check script did not return a result.";
            }

            if(!pythonFailure.empty())
            {
                PaperLogger.error(
                    "Embedded Big Screen release-check failure:\n{}",
                    pythonFailure);
                ErrorManager::Instance().RecordError(
                    "Checking Big Screen releases", pythonFailure);
                outcome.message =
                    "Big Screen could not run the release check. Details were written to the error log.";
            }
            else
            {
                rapidjson::Document result;
                result.Parse(resultJson.data(), resultJson.size());
                if(result.HasParseError() || !result.IsObject())
                    throw std::runtime_error(
                        "The GitHub release response could not be interpreted.");

                const std::string state = result.HasMember("state") &&
                    result["state"].IsString()
                    ? result["state"].GetString()
                    : "unavailable";
                const std::string message = result.HasMember("message") &&
                    result["message"].IsString()
                    ? result["message"].GetString()
                    : "The GitHub release check is currently unavailable.";
                if(state != "received")
                    outcome.message = message;
                else
                {
                    outcome.latestVersion = result.HasMember("version") &&
                        result["version"].IsString()
                        ? result["version"].GetString()
                        : std::string{};
                    if(!CoreLogic::ParseSemanticVersion(outcome.latestVersion))
                    {
                        outcome.latestVersion.clear();
                        outcome.message =
                            "GitHub's latest release does not use a recognized version number.";
                    }
                    else if(CoreLogic::IsReleaseVersionNewer(
                                outcome.currentVersion,
                                outcome.latestVersion))
                    {
                        outcome.state = ModReleaseCheckState::UpdateAvailable;
                        outcome.message =
                            "Installed: " + outcome.currentVersion +
                            "  |  Available: " + outcome.latestVersion;
                    }
                    else
                    {
                        outcome.state = ModReleaseCheckState::UpToDate;
                        outcome.message =
                            "Big Screen " + outcome.currentVersion +
                            " is current.";
                    }
                }
            }
        }
        catch(const std::exception& exception)
        {
            outcome.message =
                "Could not check Big Screen releases. Try again later.";
            PaperLogger.error(
                "Big Screen release check stopped: {}", exception.what());
            ErrorManager::Instance().RecordError(
                "Checking Big Screen releases", exception.what());
        }
        catch(...)
        {
            outcome.message =
                "Could not check Big Screen releases because of an unexpected internal error.";
            ErrorManager::Instance().RecordError(
                "Checking Big Screen releases",
                "An unknown native exception escaped the release-check worker.");
        }

        std::scoped_lock lock(modReleaseMutex_);
        modReleaseSnapshot_ = outcome;
        if(outcome.state == ModReleaseCheckState::UpdateAvailable)
        {
            modReleaseNotice_ = ModReleaseNotice{
                "Big Screen update available",
                "Current version: " + outcome.currentVersion +
                    "\nNew version: " + outcome.latestVersion +
                    "\n\nDownload and install the update through ModsBeforeFriday or the Big Screen GitHub releases page."};
        }
        else if(!automatic &&
                outcome.state == ModReleaseCheckState::UpToDate)
        {
            modReleaseNotice_ = ModReleaseNotice{
                "Big Screen is up to date",
                "Current version: " + outcome.currentVersion +
                    "\n\nNo newer stable release was found."};
        }
        else if(!automatic)
        {
            modReleaseNotice_ = ModReleaseNotice{
                "Could not check for updates",
                outcome.message};
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
                        const auto detail = TakePythonExceptionText();
                        PaperLogger.warn(
                            "Could not fetch video thumbnail for {}: {}",
                            request.levelId,
                            detail);
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

    void DownloadManager::RefreshSnapshotFromDisk()
    {
        std::scoped_lock statusReadLock(statusReadMutex_);
        std::filesystem::path statusPath;
        DownloadSnapshot refreshed;
        {
            std::scoped_lock lock(mutex_);
            if(ignoreStatusFile_)
                return;
            statusPath = statusPath_;
            refreshed = snapshot_;
        }

        // Shared-storage reads and JSON parsing deliberately happen without
        // mutex_. Unity's Snapshot() can always copy the last complete mailbox
        // even if Android storage temporarily responds slowly.
        std::ifstream stream(statusPath, std::ios::binary);
        if(!stream) return;
        const std::string json{std::istreambuf_iterator<char>(stream), {}};
        rapidjson::Document document;
        document.Parse(json.data(), json.size());
        if(document.HasParseError() || !document.IsObject()) return;
        refreshed.state = ParseState(ReadString(document, "state"));
        refreshed.message = ReadString(document, "message");
        refreshed.errorCode = ReadString(document, "errorCode");
        refreshed.diagnostic = ReadString(document, "diagnostic");
        refreshed.downloadedBytes = static_cast<std::uint64_t>(ReadNumber(document, "downloadedBytes"));
        refreshed.totalBytes = static_cast<std::uint64_t>(ReadNumber(document, "totalBytes"));
        refreshed.speedBytesPerSecond = ReadNumber(document, "speed");
        refreshed.etaSeconds = ReadNumber(document, "eta");
        const auto title = ReadString(document, "title");
        const auto thumbnailPath = ReadString(document, "thumbnailPath");
        if(!title.empty()) refreshed.title = title;
        if(!thumbnailPath.empty()) refreshed.thumbnailPath = thumbnailPath;
        const auto heights = ReadIntegerArray(document, "availableHeights");
        if(!heights.empty()) refreshed.availableHeights = heights;
        const int requestedHeight =
            static_cast<int>(ReadNumber(document, "requestedHeight"));
        if(requestedHeight > 0)
            refreshed.requestedHeight = requestedHeight;

        {
            std::scoped_lock lock(mutex_);
            if(ignoreStatusFile_ || statusPath_ != statusPath)
                return;
            snapshot_ = std::move(refreshed);
        }

        // Diagnostic writes happen only after mutex_ has been released. The
        // status worker owns this throttle state through statusReadMutex_, so
        // no additional worker or lock ordering is introduced.
        const auto published = Snapshot();
        auto& diagnostics = DiagnosticSessionLogger::Instance();
        if(diagnostics.DownloadSessionActive())
        {
            if(published.state != lastDiagnosticState_)
            {
                diagnostics.DownloadEvent(
                    "download_stage", "DownloadManager", {
                        {"previous", StateName(lastDiagnosticState_)},
                        {"stage", StateName(published.state)},
                        {"message", published.message}});
                lastDiagnosticState_ = published.state;
            }
            const auto now = std::chrono::steady_clock::now();
            bool emitProgress = false;
            int bucket = -1;
            if(published.totalBytes > 0)
            {
                const auto percentage = std::clamp(
                    static_cast<int>((published.downloadedBytes * 100ull) /
                        published.totalBytes), 0, 100);
                bucket = (percentage / 5) * 5;
                emitProgress = bucket != lastDiagnosticProgressBucket_;
            }
            else if(published.Active() &&
                    (lastUnknownSizeDiagnostic_ ==
                         std::chrono::steady_clock::time_point{} ||
                     now - lastUnknownSizeDiagnostic_ >=
                         std::chrono::seconds(5)))
            {
                emitProgress = true;
            }
            if(emitProgress)
            {
                DiagnosticFields fields{
                    {"downloadedBytes", std::to_string(
                        published.downloadedBytes)},
                    {"totalBytes", std::to_string(published.totalBytes)},
                    {"speedBytesPerSecond", std::to_string(
                        published.speedBytesPerSecond)},
                    {"etaSeconds", std::to_string(published.etaSeconds)}};
                if(bucket >= 0)
                    fields.emplace_back("percentage", std::to_string(bucket));
                diagnostics.DownloadEvent(
                    "download_progress", "DownloadManager", fields);
                lastDiagnosticProgressBucket_ = bucket;
                lastUnknownSizeDiagnostic_ = now;
            }
            if(!published.diagnostic.empty() &&
               published.diagnostic != lastDownloaderDiagnostic_)
            {
                lastDownloaderDiagnostic_ = published.diagnostic;
                diagnostics.DownloadEvent(
                    "ytdlp_message", "yt-dlp", {
                        {"severity", published.state == DownloadState::Failed
                            ? "error" : "warning"},
                        {"message", DiagnosticSessionLogger::SanitizeExternalMessage(
                            published.diagnostic)}});
            }
        }

        std::filesystem::path downloaderDiagnosticPath;
        std::uintmax_t diagnosticOffset = 0;
        {
            std::scoped_lock lock(mutex_);
            downloaderDiagnosticPath = downloaderDiagnosticPath_;
            diagnosticOffset = downloaderDiagnosticOffset_;
        }
        if(diagnostics.DownloadSessionActive() &&
           !downloaderDiagnosticPath.empty())
        {
            std::ifstream ytdlpStream(
                downloaderDiagnosticPath, std::ios::binary);
            if(ytdlpStream)
            {
                ytdlpStream.seekg(static_cast<std::streamoff>(diagnosticOffset));
                std::string line;
                while(std::getline(ytdlpStream, line))
                {
                    rapidjson::Document message;
                    message.Parse(line.data(), line.size());
                    if(message.HasParseError() || !message.IsObject())
                        continue;
                    diagnostics.DownloadEvent(
                        "ytdlp_message", "yt-dlp", {
                            {"severity", ReadString(message, "severity")},
                            {"message", DiagnosticSessionLogger::SanitizeExternalMessage(
                                ReadString(message, "message"))}});
                }
                std::error_code sizeError;
                const auto size = std::filesystem::file_size(
                    downloaderDiagnosticPath, sizeError);
                if(!sizeError)
                {
                    std::scoped_lock lock(mutex_);
                    if(downloaderDiagnosticPath_ == downloaderDiagnosticPath)
                        downloaderDiagnosticOffset_ = size;
                }
            }
        }
    }

    void DownloadManager::SetFailure(std::string message)
    {
        std::filesystem::path statusPath;
        {
            std::scoped_lock lock(mutex_);
            ignoreStatusFile_ = true;
            statusPath = statusPath_;
            snapshot_.state = DownloadState::Failed;
            snapshot_.message = message;
        }
        // The Python status file may still describe the last non-terminal
        // progress update. ignoreStatusFile_ is published with the C++ failure
        // before cleanup, so Snapshot() cannot resurrect a wedged active job
        // while this background thread removes the stale file.
        std::error_code removeError;
        std::filesystem::remove(statusPath, removeError);
        if(removeError)
        {
            // Retry a transient handle race once. If deletion still fails,
            // invalidate the JSON so Snapshot() cannot revive an active state.
            removeError.clear();
            std::filesystem::remove(statusPath, removeError);
            if(removeError)
            {
                std::ofstream staleStatus(
                    statusPath, std::ios::binary | std::ios::trunc);
                staleStatus.flush();
                const std::string cleanupDetail =
                    "Could not remove stale downloader status '" +
                    statusPath.string() + "': " + removeError.message();
                PaperLogger.warn("{}", cleanupDetail);
                ErrorManager::Instance().RecordError(
                    "Cleaning downloader status", cleanupDetail);
            }
        }
        PaperLogger.error("{}", message);
        ErrorManager::Instance().RecordError(
            "Downloader operation",
            message);
        DiagnosticSessionLogger::Instance().DownloadEvent(
            "download_failed", "DownloadManager", {
                {"message", message}});
        DiagnosticSessionLogger::Instance().EndDownloadSession(
            "failed", {{"message", message}});
    }
}
