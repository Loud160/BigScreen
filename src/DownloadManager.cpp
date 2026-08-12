#include "BigScreen/DownloadManager.hpp"

#include <Python.h>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "main.hpp"
#include "BigScreen/CoreLogic.hpp"
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
            const auto sourceSize = std::filesystem::file_size(source, fileError);
            if(fileError)
            {
                error = "Could not read " + source.filename().string() + ": " +
                        fileError.message();
                return false;
            }
            const auto destinationSize = std::filesystem::file_size(destination, fileError);
            if(!fileError && destinationSize == sourceSize)
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
            std::error_code iteratorError;
            std::filesystem::directory_iterator entries(sourceExtensions, iteratorError);
            if(iteratorError)
            {
                error = "Could not read the CPython extension folder: " +
                        iteratorError.message();
                return false;
            }
            for(const auto& entry : entries)
            {
                if(entry.path().extension() != ".so")
                    continue;
                if(!CopyNativeLibrary(
                    entry.path(),
                    privateExtensions / entry.path().filename(),
                    error))
                    return false;
            }
            return true;
        }

        constexpr const char* DownloaderScript = R"PY(
import json, os, re, shutil, traceback, urllib.request

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

try:
    publish('preparing', 'Checking video information')
    cancelled()
    # Importing this module registers Big Screen's in-process JavaScript
    # challenge provider before yt-dlp creates the YouTube extractor.
    import bigscreen_jsc_provider
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
    publish('failed', clean_error(error))
except BaseException as error:
    state, message = classify(error)
    publish(state, message)
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
        with urllib.request.urlopen(urllib.request.Request(package_url, headers={'User-Agent':'Big-Screen-Beat-Saber'}), timeout=60) as response, open(temporary, 'wb') as output:
            while True:
                block = response.read(262144)
                if not block: break
                output.write(block); digest.update(block)
        if digest.hexdigest().lower() != expected.lower():
            os.remove(temporary)
            raise RuntimeError('Downloaded yt-dlp SHA-256 did not match the official release checksum.')
        with zipfile.ZipFile(temporary) as package:
            if package.testzip() is not None or 'yt_dlp/__init__.py' not in package.namelist():
                raise RuntimeError('The downloaded yt-dlp package failed its compatibility self-test.')
        os.replace(temporary, job['nextPath'])
        with open(job['nextPath'] + '.version', 'w', encoding='utf-8') as version_file:
            version_file.write(version)
        publish('completed', 'yt-dlp ' + version + ' verified. Restart Beat Saber to activate it.', version=version)
except BaseException as error:
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
        const auto runtime = VideoLibrary::Instance().RuntimePath();
        const auto next = runtime / "yt-dlp-next";
        const auto active = runtime / "yt-dlp-active";
        const auto previous = runtime / "yt-dlp-previous";
        bool promotedCandidate = false;
        std::string candidateVersion;
        if(std::filesystem::is_regular_file(next))
        {
            std::ifstream version(next.string() + ".version");
            if(version) std::getline(version, candidateVersion);
            std::filesystem::remove(previous);
            std::filesystem::remove(previous.string() + ".version");
            if(std::filesystem::is_regular_file(active))
            {
                std::filesystem::rename(active, previous);
                if(std::filesystem::is_regular_file(active.string() + ".version"))
                    std::filesystem::rename(active.string() + ".version", previous.string() + ".version");
            }
            std::filesystem::rename(next, active);
            if(std::filesystem::is_regular_file(next.string() + ".version"))
                std::filesystem::rename(next.string() + ".version", active.string() + ".version");
            promotedCandidate = true;
        }
        std::ifstream activeVersion(active.string() + ".version");
        if(activeVersion) std::getline(activeVersion, currentUpdateVersion_);
        const auto modLibraries = std::filesystem::path(
            "/sdcard/ModData/com.beatgames.beatsaber/Modloader/libs");
        if(!StageNativeRuntime(runtime, modLibraries, error))
            return false;
        if(!LoadGlobalLibrary(InternalNativeRuntime / "libcrypto_python.so", error) ||
           !LoadGlobalLibrary(InternalNativeRuntime / "libssl_python.so", error) ||
           !LoadGlobalLibrary(InternalNativeRuntime / "libsqlite3_python.so", error))
            return false;

        // Register the compiled bridge before CPython starts. Keeping this as
        // a built-in module avoids Android's prohibition on executing a qjs
        // program from writable app storage and avoids another staged .so.
        if(PyImport_AppendInittab(
               "bigscreen_quickjs", PyInit_bigscreen_quickjs) == -1)
        {
            error = "Could not register the in-process QuickJS-NG bridge.";
            return false;
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

        // certifi is installed as a physical package so yt-dlp's unmodified
        // certifi.where() returns this same real PEM path. The environment
        // variables cover urllib and any future downloader networking backend
        // without running a fallible Python bootstrap during initialization.
        const auto certificateBundle = runtime / "certifi" / "cacert.pem";
        std::ifstream certificateStream(certificateBundle, std::ios::binary);
        std::string certificateHeader(4096, '\0');
        certificateStream.read(certificateHeader.data(), certificateHeader.size());
        certificateHeader.resize(static_cast<std::size_t>(certificateStream.gcount()));
        if(certificateHeader.find("-----BEGIN CERTIFICATE-----") == std::string::npos)
        {
            error = "The packaged certificate authority bundle is missing or invalid.";
            PyEval_SaveThread();
            return false;
        }
        const auto bundlePath = certificateBundle.string();
        if(setenv("SSL_CERT_FILE", bundlePath.c_str(), 1) != 0 ||
           setenv("REQUESTS_CA_BUNDLE", bundlePath.c_str(), 1) != 0 ||
           setenv("CURL_CA_BUNDLE", bundlePath.c_str(), 1) != 0)
        {
            error = "Could not configure the embedded certificate authority bundle.";
            PyEval_SaveThread();
            return false;
        }

        // A checksum proves that the archive matches the official release; it
        // does not prove that this CPython build can import that release. Test
        // the exact active module before accepting it as authoritative.
        const auto smokeTest = []()
        {
            return PyRun_SimpleString(
                "import importlib, json, sys\n"
                "importlib.invalidate_caches()\n"
                "[sys.modules.pop(k, None) for k in list(sys.modules) if k == 'bigscreen_jsc_provider' or k == 'yt_dlp' or k.startswith('yt_dlp.')]\n"
                "import bigscreen_quickjs\n"
                "assert json.loads(bigscreen_quickjs.execute('console.log(JSON.stringify({value: 6 * 7}))'))['value'] == 42\n"
                "import bigscreen_jsc_provider\n"
                "import yt_dlp_ejs\n"
                "assert isinstance(yt_dlp_ejs.version, str) and yt_dlp_ejs.version\n"
                "from yt_dlp.extractor.youtube.jsc._registry import _jsc_providers\n"
                "assert 'BigScreenQuickJS' in _jsc_providers.value\n"
                "from yt_dlp import YoutubeDL\n"
                "assert callable(YoutubeDL)\n") == 0;
        };
        if(!smokeTest())
        {
            PyErr_Print();
            PyErr_Clear();
            if(promotedCandidate)
            {
                // Restore the one previous working package, or fall back to
                // the immutable shipped baseline when no previous update
                // exists. Normal video/network failures never enter this path.
                std::filesystem::remove(active);
                std::filesystem::remove(active.string() + ".version");
                if(std::filesystem::is_regular_file(previous))
                {
                    std::filesystem::rename(previous, active);
                    if(std::filesystem::is_regular_file(previous.string() + ".version"))
                        std::filesystem::rename(
                            previous.string() + ".version",
                            active.string() + ".version");
                }
                currentUpdateVersion_ = "2026.07.04";
                std::ifstream restoredVersion(active.string() + ".version");
                if(restoredVersion)
                    std::getline(restoredVersion, currentUpdateVersion_);
                std::ofstream rejected(
                    runtime / "yt-dlp-rejected.version",
                    std::ios::binary | std::ios::trunc);
                rejected << (candidateVersion.empty() ? "unknown" : candidateVersion);
                rejected.close();
                if(!smokeTest())
                {
                    PyErr_Print();
                    PyErr_Clear();
                    error = "Neither the previous nor shipped yt-dlp package could be imported.";
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
                error = "The shipped yt-dlp package could not be imported.";
                PyEval_SaveThread();
                return false;
            }
        }
        else if(promotedCandidate)
        {
            // A candidate is not trusted merely because it downloaded and
            // matched its checksum. Clear the rejection marker only after the
            // newly activated package imports successfully on this headset.
            std::filesystem::remove(runtime / "yt-dlp-rejected.version");
        }
        PyEval_SaveThread();
        initialized_ = true;
        PaperLogger.info(
            "Embedded CPython downloader initialized with QuickJS-NG {} and CA bundle '{}'",
            QuickJsVersion,
            certificateBundle.string());
        return true;
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
            error = "The embedded downloader runtime is not available.";
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
        if(!initialized_) { error = "The embedded downloader runtime is unavailable."; return false; }
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
        RefreshSnapshotFromDiskLocked();
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

            const auto gil = PyGILState_Ensure();
            PyObject* globals = PyDict_New();
            PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
            auto* job = PyUnicode_FromStringAndSize(buffer.GetString(), buffer.GetSize());
            PyDict_SetItemString(globals, "BIGSCREEN_JOB", job);
            Py_DECREF(job);
            auto* result = PyRun_String(DownloaderScript, Py_file_input, globals, globals);
            bool runtimeRolledBack = false;
            if(!result)
            {
                PyErr_Print();
                PyErr_Clear();
                // A Python execution/import failure is an internal downloader
                // failure, unlike a private video or network error (those are
                // caught and written to the normal status JSON by the script).
                // Restore the one retained package and retry this job once.
                const auto runtime = VideoLibrary::Instance().RuntimePath();
                const auto active = runtime / "yt-dlp-active";
                const auto previous = runtime / "yt-dlp-previous";
                if(std::filesystem::is_regular_file(previous))
                {
                    std::filesystem::remove(active);
                    std::filesystem::remove(active.string() + ".version");
                    std::filesystem::rename(previous, active);
                    if(std::filesystem::is_regular_file(previous.string() + ".version"))
                        std::filesystem::rename(
                            previous.string() + ".version",
                            active.string() + ".version");
                    PyRun_SimpleString(
                        "import importlib, sys\n"
                        "importlib.invalidate_caches()\n"
                        "[sys.modules.pop(k, None) for k in list(sys.modules) if k == 'bigscreen_jsc_provider' or k == 'yt_dlp' or k.startswith('yt_dlp.')]\n");
                    result = PyRun_String(
                        DownloaderScript, Py_file_input, globals, globals);
                    runtimeRolledBack = result != nullptr;
                    if(!result)
                    {
                        PyErr_Print();
                        PyErr_Clear();
                    }
                }
            }
            const bool runtimeFailed = result == nullptr;
            if(result) Py_DECREF(result);
            Py_DECREF(globals);
            PyGILState_Release(gil);

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

            const auto gil = PyGILState_Ensure();
            PyObject* globals = PyDict_New();
            PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
            auto* job = PyUnicode_FromStringAndSize(buffer.GetString(), buffer.GetSize());
            PyDict_SetItemString(globals, "BIGSCREEN_JOB", job);
            Py_DECREF(job);
            auto* result = PyRun_String(ProbeScript, Py_file_input, globals, globals);
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
            const auto gil = PyGILState_Ensure();
            PyObject* globals = PyDict_New();
            PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
            auto* job = PyUnicode_FromStringAndSize(buffer.GetString(), buffer.GetSize());
            PyDict_SetItemString(globals, "BIGSCREEN_JOB", job);
            Py_DECREF(job);
            auto* result = PyRun_String(UpdaterScript, Py_file_input, globals, globals);
            if(!result) { PyErr_Print(); PyErr_Clear(); } else Py_DECREF(result);
            Py_DECREF(globals);
            PyGILState_Release(gil);
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

                const auto gil = PyGILState_Ensure();
                PyObject* globals = PyDict_New();
                PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
                auto* job = PyUnicode_FromStringAndSize(
                    buffer.GetString(), buffer.GetSize());
                PyDict_SetItemString(globals, "BIGSCREEN_JOB", job);
                Py_DECREF(job);
                auto* result = PyRun_String(
                    ThumbnailScript, Py_file_input, globals, globals);
                if(!result)
                {
                    PyErr_Clear();
                    PaperLogger.warn(
                        "Could not fetch video thumbnail for {}",
                        request.levelId);
                }
                else
                {
                    Py_DECREF(result);
                    PaperLogger.debug(
                        "Cached video thumbnail for {}",
                        request.levelId);
                }
                Py_DECREF(globals);
                PyGILState_Release(gil);
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
    }
}
