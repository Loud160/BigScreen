#include "BigScreen/DownloadManager.hpp"

#include <Python.h>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "main.hpp"

namespace BigScreen {
    namespace {
        constexpr std::uint64_t RequiredReserve = 512ull * 1024ull * 1024ull;

        const char* StateName(DownloadState state)
        {
            switch(state)
            {
                case DownloadState::Preparing: return "preparing";
                case DownloadState::Downloading: return "downloading";
                case DownloadState::Completed: return "completed";
                case DownloadState::Cancelled: return "cancelled";
                case DownloadState::Failed: return "failed";
                default: return "idle";
            }
        }

        DownloadState ParseState(const std::string& state)
        {
            if(state == "preparing") return DownloadState::Preparing;
            if(state == "downloading") return DownloadState::Downloading;
            if(state == "completed") return DownloadState::Completed;
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

        constexpr const char* DownloaderScript = R"PY(
import json, os, re, shutil, traceback

job = json.loads(BIGSCREEN_JOB)
status_path = job['statusPath']
cancel_path = job['cancelPath']

def publish(state, message='', **values):
    data = {'state': state, 'message': message}
    data.update(values)
    temporary = status_path + '.tmp'
    with open(temporary, 'w', encoding='utf-8') as stream:
        json.dump(data, stream, ensure_ascii=False)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, status_path)

def cancelled():
    if os.path.exists(cancel_path):
        raise RuntimeError('BIGSCREEN_CANCELLED')

def progress(data):
    cancelled()
    if data.get('status') == 'downloading':
        publish(
            'downloading',
            'Downloading video',
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
    if 'sign in to confirm your age' in lower or 'age-restricted' in lower:
        return 'failed', 'YouTube requires a signed-in account for this age-restricted video; Big Screen cannot download logged-in videos.'
    if 'private video' in lower:
        return 'failed', 'This YouTube video is private and cannot be downloaded.'
    if 'not available in your country' in lower or 'geo' in lower and 'restricted' in lower:
        return 'failed', 'This video is not available in your region.'
    if 'requested format is not available' in lower or 'no video formats' in lower:
        return 'failed', 'This video has no H.264 MP4 stream at 1080p or lower.'
    if 'no space left' in lower:
        return 'failed', 'The Quest ran out of free storage while downloading.'
    if 'http error 429' in lower or 'too many requests' in lower:
        return 'failed', 'YouTube is temporarily rate-limiting this headset. Try again later.'
    if 'certificate verify failed' in lower:
        return 'failed', 'Secure connection failed because the certificate could not be verified.'
    if 'unable to download' in lower or 'network is unreachable' in lower or 'timed out' in lower:
        return 'failed', 'Network download failed: ' + text
    return 'failed', 'YouTube download failed: ' + text

try:
    publish('preparing', 'Checking video information')
    cancelled()
    import yt_dlp
    selector = 'bestvideo[ext=mp4][vcodec^=avc1][height<=1080]'
    common = {
        'quiet': True,
        'no_warnings': True,
        'noplaylist': True,
        'format': selector,
        'continuedl': True,
        'nopart': False,
        'progress_hooks': [progress],
        'overwrites': True,
    }
    with yt_dlp.YoutubeDL(dict(common, skip_download=True)) as probe:
        info = probe.extract_info(job['sourceUrl'], download=False)
    age_limit = int(info.get('age_limit') or 0)
    if age_limit >= 18 and not job.get('explicitContentAllowed', False):
        raise PermissionError('Big Screen blocked this age-restricted video because explicit content is disabled in Beat Saber parental controls.')
    formats = [f for f in (info.get('formats') or [])
               if f.get('ext') == 'mp4'
               and str(f.get('vcodec') or '').startswith('avc1')
               and int(f.get('height') or 0) <= 1080
               and f.get('acodec') == 'none']
    if not formats:
        raise RuntimeError('Requested format is not available: no H.264 MP4 video-only stream at 1080p or lower')
    chosen = max(formats, key=lambda f: (int(f.get('height') or 0), int(f.get('width') or 0), float(f.get('tbr') or 0)))
    expected = int(chosen.get('filesize') or chosen.get('filesize_approx') or 0)
    free = shutil.disk_usage(os.path.dirname(job['finalPath'])).free
    required = expected + int(job['reserveBytes']) if expected else int(job['unknownRequiredBytes'])
    if free < required:
        raise OSError('Not enough free Quest storage. Need at least %.1f MB free; %.1f MB is available.' % (required / 1048576, free / 1048576))
    options = dict(common, outtmpl=job['finalPath'], format=chosen['format_id'])
    with yt_dlp.YoutubeDL(options) as downloader:
        result = downloader.extract_info(job['sourceUrl'], download=True)
    cancelled()
    size = os.path.getsize(job['finalPath'])
    publish(
        'completed',
        'Video downloaded',
        title=result.get('title') or info.get('title') or '',
        duration=result.get('duration') or info.get('duration') or 0,
        width=chosen.get('width') or 0,
        height=chosen.get('height') or 0,
        codec=chosen.get('vcodec') or 'h264',
        bytes=size,
        downloadedBytes=size,
        totalBytes=size)
except PermissionError as error:
    publish('failed', clean_error(error))
except BaseException as error:
    state, message = classify(error)
    publish(state, message)
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
    }

    bool DownloadManager::Initialize(std::string& error)
    {
        if(initialized_) return true;
        const auto runtime = VideoLibrary::Instance().RuntimePath();
        const auto modLibraries = std::filesystem::path(
            "/sdcard/ModData/com.beatgames.beatsaber/Modloader/libs");
        if(!LoadGlobalLibrary(modLibraries / "libcrypto_python.so", error) ||
           !LoadGlobalLibrary(modLibraries / "libssl_python.so", error) ||
           !LoadGlobalLibrary(modLibraries / "libsqlite3_python.so", error))
            return false;

        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);
        config.module_search_paths_set = 1;
        const std::array paths{
            runtime / "python314.zip",
            runtime / "lib-dynload",
            runtime / "certifi.whl",
            runtime / "yt-dlp-active",
            runtime / "yt-dlp-shipped"
        };
        for(const auto& path : paths)
        {
            const auto wide = Py_DecodeLocale(path.c_str(), nullptr);
            if(!wide)
            {
                error = "Could not encode Python runtime path";
                PyConfig_Clear(&config);
                return false;
            }
            const auto status = PyWideStringList_Append(&config.module_search_paths, wide);
            PyMem_RawFree(wide);
            if(PyStatus_Exception(status))
            {
                error = status.err_msg ? status.err_msg : "Could not configure Python path";
                PyConfig_Clear(&config);
                return false;
            }
        }
        auto status = Py_InitializeFromConfig(&config);
        PyConfig_Clear(&config);
        if(PyStatus_Exception(status))
        {
            error = status.err_msg ? status.err_msg : "Python initialization failed";
            return false;
        }
        PyEval_SaveThread();
        initialized_ = true;
        PaperLogger.info("Embedded CPython downloader initialized");
        return true;
    }

    bool DownloadManager::Start(DownloadRequest request, std::string& error)
    {
        if(!initialized_)
        {
            error = "The embedded downloader runtime is not available.";
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
        snapshot_ = {DownloadState::Preparing, request.levelId, "Checking video information"};
        worker_ = std::thread([this, request = std::move(request), finalPath]() mutable {
            Run(std::move(request), finalPath);
        });
        return true;
    }

    void DownloadManager::Cancel()
    {
        std::scoped_lock lock(mutex_);
        if(!snapshot_.Active()) return;
        std::ofstream(cancelPath_, std::ios::binary | std::ios::trunc) << "cancel";
    }

    DownloadSnapshot DownloadManager::Snapshot()
    {
        std::scoped_lock lock(mutex_);
        RefreshSnapshotFromDiskLocked();
        return snapshot_;
    }

    void DownloadManager::Run(DownloadRequest request, std::filesystem::path finalPath)
    {
        try
        {
            rapidjson::Document document(rapidjson::kObjectType);
            auto& allocator = document.GetAllocator();
            AddString(document, "sourceUrl", request.sourceUrl, allocator);
            AddString(document, "finalPath", finalPath.string(), allocator);
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

            const auto gil = PyGILState_Ensure();
            PyObject* globals = PyDict_New();
            PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
            auto* job = PyUnicode_FromStringAndSize(buffer.GetString(), buffer.GetSize());
            PyDict_SetItemString(globals, "BIGSCREEN_JOB", job);
            Py_DECREF(job);
            auto* result = PyRun_String(DownloaderScript, Py_file_input, globals, globals);
            if(!result)
            {
                PyErr_Print();
                PyErr_Clear();
            }
            else Py_DECREF(result);
            Py_DECREF(globals);
            PyGILState_Release(gil);

            std::scoped_lock lock(mutex_);
            RefreshSnapshotFromDiskLocked();
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
        }
        catch(const std::exception& exception)
        {
            SetFailure(std::string("Downloader stopped: ") + exception.what());
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
    }

    void DownloadManager::SetFailure(std::string message)
    {
        std::scoped_lock lock(mutex_);
        snapshot_.state = DownloadState::Failed;
        snapshot_.message = std::move(message);
        PaperLogger.error("{}", snapshot_.message);
    }
}
