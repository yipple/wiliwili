//
// Created by fang on 2023/7/18.
//

#include <borealis/views/dialog.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/core/logger.hpp>

#include "activity/dlna_activity.hpp"
#include "bilibili/util/uuid.hpp"
#include "utils/config_helper.hpp"
#include "utils/number_helper.hpp"
#include "view/video_view.hpp"
#include "view/mpv_core.hpp"

using namespace brls::literals;

#define GET_SETTING ProgramConfig::instance().getSettingItem

DLNAActivity::DLNAActivity() {
    GA("open_dlna")

    MPVCore::instance().reset();
    MPVCore::instance().setAspect(
        ProgramConfig::instance().getSettingItem(SettingItem::PLAYER_ASPECT, std::string{"-1"}));

    ip = brls::Application::getPlatform()->getIpAddress();
    ip = GET_SETTING(SettingItem::DLNA_IP, ip);
    brls::Logger::info("DLNA IP: {}", ip);

    port = 9958;
    port = GET_SETTING(SettingItem::DLNA_PORT, port);
    brls::Logger::info("DLNA Port: {}", port);

    uuid = "uuid:" + bilibili::genUUID(ProgramConfig::instance().getClientID());
    brls::Logger::info("DLNA UUID: {}", uuid);

    std::string defaultName = "wiliwili " + APPVersion::instance().getPlatform();
    std::string name        = GET_SETTING(SettingItem::DLNA_NAME, defaultName);

    dlna = std::make_shared<pdr::DLNA>(ip, port, uuid);
    dlna->setDeviceInfo("friendlyName", name);
    dlna->setDeviceInfo("manufacturer", "xfangfang");
    dlna->setDeviceInfo("manufacturerURL", "https://github.com/xfangfang");
    dlna->setDeviceInfo("modelDescription", "wiliwili DMR");
    dlna->setDeviceInfo("modelName", "wiliwili");
    dlna->setDeviceInfo("modelNumber", APPVersion::instance().getVersionStr());
    dlna->setDeviceInfo("modelURL", "https://github.com/xfangfang/wiliwili");
    dlna->start();

    dlnaEventSubscribeID = DLNA_EVENT.subscribe([this](const std::string& event, void* data) {
        if (event == "CurrentURI") {
            std::string url = std::string{(char*)data};
            brls::Logger::info("CurrentURI: {}", url);
            brls::sync([this, url]() {
                MPVCore::instance().reset();
                video->setTitle("wiliwili/setting/tools/others/dlna"_i18n);
                video->showOSD(true);
                MPVCore::instance().setUrl(url);
            });
        } else if (event == "CurrentURIMetaData") {
            std::string name = std::string{(char*)data};
            brls::sync([this, name]() { video->setTitle(name); });
        } else if (event == "Stop") {
            brls::sync([this]() {
                MPVCore::instance().pause();
                video->showOSD(false);
                video->setTitle("wiliwili/setting/tools/others/dlna_waiting"_i18n);
            });
        } else if (event == "Play") {
            brls::sync([]() { MPVCore::instance().resume(); });
            std::string value = "PLAYING";
            PLAYER_EVENT.fire("TransportState", (void*)value.c_str());
        } else if (event == "Pause") {
            brls::sync([]() { MPVCore::instance().pause(); });
            std::string value = "PAUSED_PLAYBACK";
            PLAYER_EVENT.fire("TransportState", (void*)value.c_str());
        } else if (event == "Seek") {
            std::string position = std::string{(char*)data};
            brls::sync([position]() { MPVCore::instance().seek(position); });
        } else if (event == "SetVolume") {
            std::string volume = std::string{(const char*)data};
            brls::sync([volume]() {
                MPVCore::instance().setVolume(volume);
                MPVCore::instance().showOsdText("Volume: " + volume);
            });
        } else if (event == "Error") {
            std::string msg = std::string{(const char*)data};
            brls::sync([this, msg]() {
                video->showOSD(false);
                video->setTitle("[Error] " + msg);
            });
        }
    });

    mpvEventSubscribeID = MPV_E->subscribe([](MpvEventEnum event) {
        switch (event) {
            case MpvEventEnum::MPV_RESUME: {
                std::string value = "PLAYING";
                PLAYER_EVENT.fire("TransportState", (void*)value.c_str());
                break;
            }
            case MpvEventEnum::MPV_PAUSE: {
                std::string value = "PAUSED_PLAYBACK";
                PLAYER_EVENT.fire("TransportState", (void*)value.c_str());
                break;
            }
            case MpvEventEnum::START_FILE: {
                std::string value = "TRANSITIONING";
                PLAYER_EVENT.fire("TransportState", (void*)value.c_str());
                break;
            }
            case MpvEventEnum::END_OF_FILE:
            case MpvEventEnum::MPV_STOP: {
                std::string value = "STOPPED";
                PLAYER_EVENT.fire("TransportState", (void*)value.c_str());
                break;
            }
            case MpvEventEnum::UPDATE_DURATION: {
                std::string value = wiliwili::sec2TimeDLNA(MPVCore::instance().duration);
                PLAYER_EVENT.fire("CurrentTrackDuration", (void*)value.c_str());
                break;
            }
            case MpvEventEnum::UPDATE_PROGRESS: {
                std::string value = wiliwili::sec2TimeDLNA(MPVCore::instance().video_progress);
                PLAYER_EVENT.fire("AbsoluteTimePosition", (void*)value.c_str());
                PLAYER_EVENT.fire("RelativeTimePosition", (void*)value.c_str());
                break;
            }
            case MpvEventEnum::VIDEO_SPEED_CHANGE:
                break;
            case MpvEventEnum::VIDEO_VOLUME_CHANGE: {
                int number = (int)MPVCore::instance().getVolume();
                PLAYER_EVENT.fire("Volume", &number);
                break;
            }
            default:
                break;
        }
    });

    // 初始化状态
    std::string value;
    value = "STOPPED";
    PLAYER_EVENT.fire("TransportState", (void*)value.c_str());
    value = "1";
    PLAYER_EVENT.fire("TransportPlaySpeed", (void*)value.c_str());
    value = "OK";
    PLAYER_EVENT.fire("TransportStatus", (void*)value.c_str());
    value = "0:00:00";
    PLAYER_EVENT.fire("AbsoluteTimePosition", (void*)value.c_str());
    PLAYER_EVENT.fire("RelativeTimePosition", (void*)value.c_str());
    PLAYER_EVENT.fire("CurrentTrackDuration", (void*)value.c_str());
    int number = 2147483647;
    PLAYER_EVENT.fire("AbsoluteCounterPosition", &number);
    PLAYER_EVENT.fire("RelativeCounterPosition", &number);
    number = (int)MPVCore::instance().getVolume();
    PLAYER_EVENT.fire("Volume", &number);
    value = "http-get:*:image/x-ycbcr-yuv420:*,http-get:*:image/x-xpixmap:*,http-get:*:image/x-xfig:*,http-get:*:image/x-xbm:*,http-get:*:image/x-xbitmap:*,http-get:*:image/x-wmf:*,http-get:*:image/x-windows-bmp:*,http-get:*:image/x-rgb:*,http-get:*:image/x-quicktime:*,http-get:*:image/x-psd:*,http-get:*:image/x-portable-pixmap:*,http-get:*:image/x-portable-graymap:*,http-get:*:image/x-portable-bitmap:*,http-get:*:image/x-portable-anymap:*,http-get:*:image/x-png:*,http-get:*:image/x-pict:*,http-get:*:image/x-photoshop:*,http-get:*:image/x-pcx:*,http-get:*:image/x-ms-bmp:*,http-get:*:image/x-jg:*,http-get:*:image/x-icon:*,http-get:*:image/xicon:*,http-get:*:image/x-ico:*,http-get:*:image/x-guffaw:*,http-get:*:image/x-eps:*,http-get:*:image/x-emf:*,http-get:*:image/x.djvu:*,http-get:*:image/x-djvu:*,http-get:*:image/x-dcraw:*,http-get:*:image/x-citrix-pjpeg:*,http-get:*:image/x-bmp:*,http-get:*:image/x-bitmap:*,http-get:*:image/vnd.wap.wbmp:*,http-get:*:image/vnd.ms-photo:*,http-get:*:image/vnd.ms-modi:*,http-get:*:image/vnd.microsoft.icon:*,http-get:*:image/vnd.dxf:*,http-get:*:image/vnd.dwg:*,http-get:*:image/vnd.djvu:*,http-get:*:image/vnd.adobe.photoshop:*,http-get:*:image/tiff:*,http-get:*:image/svg+xml:*,http-get:*:image/png:*,http-get:*:image/pjpeg:*,http-get:*:image/pict:*,http-get:*:image/pdf:*,http-get:*:image/jpg:*,http-get:*:image/jpeg-cmyk:*,http-get:*:image/jpeg:*,http-get:*:image/jp2:*,http-get:*:image/icon:*,http-get:*:image/ico:*,http-get:*:image/GIF:*,http-get:*:image/gif:*,http-get:*:image/fits:*,http-get:*:image/cur:*,http-get:*:image/bmp:*,http-get:*:image/bitmap:*,http-get:*:application/x-shockwave-flash:*,http-get:*:video/m3u8:*,http-get:*:video/ogm:*,http-get:*:video/hlv:*,http-get:*:video/wtv:*,http-get:*:video/x-rmvb:*,http-get:*:video/rmvb:*,http-get:*:video/x-rm:*,http-get:*:video/rm:*,http-get:*:video/x-nerodigital-ps:*,http-get:*:video/wt:*,http-get:*:video/x-matroska:*,http-get:*:video/mkv:*,http-get:*:video/x-mkv:*,http-get:*:video/x-ms-avi:*,http-get:*:video/x-xvid:*,http-get:*:video/xvid:*,http-get:*:video/x-divx:*,http-get:*:video/divx:*,http-get:*:video/x-motion-jpeg:*,http-get:*:video/vnd.dlna.mpeg-tts:*,http-get:*:video/x-swf:*,http-get:*:video/x-sgi-movie:*,http-get:*:video/x-ms-video:*,http-get:*:video/x-pn-realvideo:*,http-get:*:video/x-pn-realaudio:*,http-get:*:video/x-ms-wvx:*,http-get:*:video/x-ms-wmx:*,http-get:*:video/x-ms-wmv:*,http-get:*:video/x-ms-wma:*,http-get:*:video/x-ms-wm:*,http-get:*:video/x-msvideo:*,http-get:*:video/x-ms-asx:*,http-get:*:video/x-ms-asf:*,http-get:*:video/mp2p:*,http-get:*:video/MP2T:*,http-get:*:video/mpeg2:*,http-get:*:video/x-mpeg:*,http-get:*:video/x-mp4:*,http-get:*:video/x-m4v:*,http-get:*:video/x-flv:*,http-get:*:video/x-dv:*,http-get:*:video/wmv:*,http-get:*:video/webm:*,http-get:*:video/vnd.objectvideo:*,http-get:*:video/unknown:*,http-get:*:video/swf:*,http-get:*:video/quicktime:*,http-get:*:video/msvideo:*,http-get:*:video/mpg4:*,http-get:*:video/mpeg4:*,http-get:*:video/mpeg3:*,http-get:*:video/mpeg:*,http-get:*:video/mp4v-es:*,http-get:*:video/mp4:*,http-get:*:video/m4v:*,http-get:*:video/flv:*,http-get:*:video/f4v:*,http-get:*:video/avi:*,http-get:*:video/asx:*,http-get:*:video/3gpp2:*,http-get:*:video/3gpp:*,http-get:*:video/ape:*,http-get:*:video/aiff:*,http-get:*:video/ra:*,http-get:*:video/flac:*,http-get:*:video/ac3:*,http-get:*:video/aac:*,http-get:*:video/ogg:*,http-get:*:video/m4a:*,http-get:*:video/wav:*,http-get:*:video/asf:*,http-get:*:video/wma:*,http-get:*:video/mp3:*,http-get:*:audio/ape:*,http-get:*:audio/x-asf-pf:*,http-get:*:audio/wma:*,http-get:*:audio/x-wav:*,http-get:*:audio/vorbis:*,http-get:*:audio/x-scpls:*,http-get:*:audio/x-ra:*,http-get:*:audio/ra:*,http-get:*:audio/x-realaudio:*,http-get:*:audio/x-pn-realaudio-plugin:*,http-get:*:audio/x-pn-realaudio:*,http-get:*:audio/x-ms-wmv:*,http-get:*:audio/x-ms-wma:*,http-get:*:audio/x-ms-wax:*,http-get:*:audio/x-mpeg-url:*,http-get:*:audio/x-mpeg3:*,http-get:*:audio/x-mp3:*,http-get:*:audio/x-midi:*,http-get:*:audio/x-m4a:*,http-get:*:audio/x-flac:*,http-get:*:audio/flac:*,http-get:*:audio/x-ac3:*,http-get:*:audio/ac3:*,http-get:*:audio/x-aac:*,http-get:*:audio/aac:*,http-get:*:audio/x-aiff:*,http-get:*:audio/wave:*,http-get:*:audio/wav:*,http-get:*:audio/vnd.rn-realaudio:*,http-get:*:audio/vnd.qcelp:*,http-get:*:audio/vnd.dlna.adts:*,http-get:*:audio/unknown:*,http-get:*:audio/playlist:*,http-get:*:audio/x-ogg:*,http-get:*:audio/ogg:*,http-get:*:audio/mpg:*,http-get:*:audio/mpeg-url:*,http-get:*:audio/mpeg3:*,http-get:*:audio/mpeg2:*,http-get:*:audio/x-mpeg:*,http-get:*:audio/mpeg:*,http-get:*:audio/mp4a-latm:*,http-get:*:audio/mp4:*,http-get:*:audio/mp3:*,http-get:*:audio/mp2:*,http-get:*:audio/mp1:*,http-get:*:audio/x-dts:*,http-get:*:audio/midi:*,http-get:*:audio/mid:*,http-get:*:audio/m4a:*,http-get:*:audio/x-atrac3:*,http-get:*:audio/basic:*,http-get:*:audio/asf:*,http-get:*:audio/aiff:*,http-get:*:audio/L16:*,http-get:*:audio/L8:*,http-get:*:audio/L16:DLNA.ORG_PN=LPCM,http-get:*:audio/L16:DLNA.ORG_PN=LPCM_low,http-get:*:audio/L16;rate=44100;channels=1:DLNA.ORG_PN=LPCM,http-get:*:audio/L16;rate=44100;channels=2:DLNA.ORG_PN=LPCM,http-get:*:audio/L16;rate=48000;channels=2:DLNA.ORG_PN=LPCM,http-get:*:audio/mpeg:DLNA.ORG_PN=MP3,http-get:*:audio/mpeg:DLNA.ORG_PN=MP3X,http-get:*:audio/vnd.dlna.adts:DLNA.ORG_PN=AAC_ADTS,http-get:*:audio/vnd.dlna.adts:DLNA.ORG_PN=AAC_ADTS_192,http-get:*:audio/vnd.dlna.adts:DLNA.ORG_PN=AAC_ADTS_320,http-get:*:audio/vnd.dlna.adts:DLNA.ORG_PN=AAC_MULT5_ADTS,http-get:*:audio/mp4:DLNA.ORG_PN=AAC_ISO,http-get:*:audio/3gpp:DLNA.ORG_PN=AAC_ISO,http-get:*:audio/mp4:DLNA.ORG_PN=AAC_ISO_192,http-get:*:audio/3gpp:DLNA.ORG_PN=AAC_ISO_192,http-get:*:audio/mp4:DLNA.ORG_PN=AAC_ISO_320,http-get:*:audio/3gpp:DLNA.ORG_PN=AAC_ISO_320,http-get:*:audio/mp4:DLNA.ORG_PN=AAC_MULT5_ISO,http-get:*:audio/3gpp:DLNA.ORG_PN=AAC_MULT5_ISO,http-get:*:audio/mp4:DLNA.ORG_PN=HEAACv2_L2,http-get:*:audio/3gpp:DLNA.ORG_PN=HEAACv2_L2,http-get:*:audio/mp4:DLNA.ORG_PN=HEAACv2_L2_128,http-get:*:audio/3gpp:DLNA.ORG_PN=HEAACv2_L2_128,http-get:*:audio/mp4:DLNA.ORG_PN=HEAACv2_L2_320,http-get:*:audio/3gpp:DLNA.ORG_PN=HEAACv2_L2_320,http-get:*:audio/mp4:DLNA.ORG_PN=HEAACv2_L3,http-get:*:audio/3gpp:DLNA.ORG_PN=HEAACv2_L3,http-get:*:audio/mp4:DLNA.ORG_PN=HEAACv2_L4,http-get:*:audio/3gpp:DLNA.ORG_PN=HEAACv2_L4,http-get:*:audio/mp4:DLNA.ORG_PN=HEAACv2_MULT5,http-get:*:audio/3gpp:DLNA.ORG_PN=HEAACv2_MULT5,http-get:*:audio/mp4:DLNA.ORG_PN=HEAAC_L2_ISO,http-get:*:audio/3gpp:DLNA.ORG_PN=HEAAC_L2_ISO,http-get:*:audio/mp4:DLNA.ORG_PN=HEAAC_L2_ISO_128,http-get:*:audio/3gpp:DLNA.ORG_PN=HEAAC_L2_ISO_128,http-get:*:audio/mp4:DLNA.ORG_PN=HEAAC_L2_ISO_320,http-get:*:audio/3gpp:DLNA.ORG_PN=HEAAC_L2_ISO_320,http-get:*:audio/mp4:DLNA.ORG_PN=HEAAC_L3_ISO,http-get:*:audio/3gpp:DLNA.ORG_PN=HEAAC_L3_ISO,http-get:*:audio/mp4:DLNA.ORG_PN=HEAAC_MULT5_ISO,http-get:*:audio/3gpp:DLNA.ORG_PN=HEAAC_MULT5_ISO,http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMAFULL,http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMABASE,http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMAPRO,http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMALSL,http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMALSL_MULT5,http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMDRM_WMABASE,http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMDRM_WMAFULL,http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMDRM_WMALSL,http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMDRM_WMALSL_MULT5,http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMDRM_WMAPRO,http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_TN,http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_SM,http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_SM_ICO,http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_MED,http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_LRG,http-get:*:image/jpeg:DLNA.ORG_PN=JPEG_LRG_ICO,http-get:*:image/png:DLNA.ORG_PN=PNG_LRG,http-get:*:image/png:DLNA.ORG_PN=PNG_LRG_ICO,http-get:*:image/png:DLNA.ORG_PN=PNG_TN,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMDRM_WMVSPML_BASE,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMDRM_WMVSPLL_BASE,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMDRM_WMVMED_PRO,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMDRM_WMVHIGH_FULL,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMDRM_WMVMED_FULL,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMDRM_WMVHIGH_PRO,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMDRM_WMVHM_BASE,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMDRM_WMVMED_BASE,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVMED_PRO,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMDRM_WMVSPML_MP3,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVMED_BASE,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVHIGH_FULL,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVHIGH_PRO,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVMED_FULL,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVSPML_MP3,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVSPML_BASE,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVSPLL_BASE,http-get:*:video/x-ms-wmv:DLNA.ORG_PN=WMVHM_BASE,http-get:*:video/x-ms-asf:DLNA.ORG_PN=VC1_ASF_AP_L2_WMA,http-get:*:video/x-ms-asf:DLNA.ORG_PN=VC1_ASF_AP_L1_WMA,http-get:*:video/x-ms-asf:DLNA.ORG_PN=WMDRM_VC1_ASF_AP_L1_WMA,http-get:*:video/x-ms-asf:DLNA.ORG_PN=WMDRM_VC1_ASF_AP_L2_WMA,http-get:*:video/x-ms-asf:DLNA.ORG_PN=MPEG4_P2_ASF_ASP_L4_SO_G726,http-get:*:video/x-ms-asf:DLNA.ORG_PN=MPEG4_P2_ASF_SP_G726,http-get:*:video/x-ms-asf:DLNA.ORG_PN=MPEG4_P2_ASF_ASP_L5_SO_G726,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_BL_CIF15_AAC_540_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_BL_CIF15_AAC_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_BL_CIF30_AAC_940_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_BL_CIF30_AAC_MULT5_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_BL_CIF30_AC3_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_BL_CIF30_MPEG1_L3_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_HD_AAC_MULT5_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_HD_AC3_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_HD_MPEG1_L3_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_SD_AAC_MULT5_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_SD_AC3_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=AVC_TS_MP_SD_MPEG1_L3_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG1,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG4_P2_TS_SP_MPEG1_L3_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG4_P2_TS_SP_AC3_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG4_P2_TS_SP_MPEG2_L2_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_PAL_XAC3,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_PAL,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_NTSC,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_NTSC_XAC3,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_HD_KO_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_HD_KO_XAC3_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_HD_NA_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_HD_NA_XAC3_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_MP_LL_AAC_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_SD_EU_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_SD_KO_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_SD_KO_XAC3_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_SD_NA_ISO,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_TS_SD_NA_XAC3_ISO,http-get:*:video/3gpp:DLNA.ORG_PN=AVC_3GPP_BL_QCIF15_AAC,http-get:*:video/3gpp:DLNA.ORG_PN=AVC_3GPP_BL_QCIF15_HEAAC,http-get:*:video/3gpp:DLNA.ORG_PN=MPEG4_P2_3GPP_SP_L0B_AAC,http-get:*:video/3gpp:DLNA.ORG_PN=MPEG4_P2_3GPP_SP_L0B_AMR,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_CIF15_AAC,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_CIF15_AAC_350,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_CIF15_AAC_520,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_CIF15_HEAAC,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_CIF15_HEAAC_350,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_CIF30_AAC_940,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_CIF30_AAC_MULT5,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_CIF30_HEAAC_L2,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_CIF30_MPEG1_L3,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L12_CIF15_HEAAC,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L12_CIF15_HEAACv2,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L12_CIF15_HEAACv2_350,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L1B_QCIF15_HEAAC,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L1B_QCIF15_HEAACv2,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L2_CIF30_AAC,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L31_HD_AAC,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L32_HD_AAC,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L3L_SD_AAC,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L3L_SD_HEAAC,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_BL_L3_SD_AAC,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_MP_SD_AAC_LC,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_MP_SD_AAC_MULT5,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_MP_SD_HEAAC_L2,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_MP_SD_HEAAC_L4,http-get:*:video/mp4:DLNA.ORG_PN=AVC_MP4_MP_SD_MPEG1_L3,http-get:*:video/mp4:DLNA.ORG_PN=MPEG4_P2_MP4_ASP_AAC,http-get:*:video/mp4:DLNA.ORG_PN=MPEG4_P2_MP4_ASP_HEAAC,http-get:*:video/mp4:DLNA.ORG_PN=MPEG4_P2_MP4_ASP_HEAAC_MULT5,http-get:*:video/mp4:DLNA.ORG_PN=MPEG4_P2_MP4_SP_AAC,http-get:*:video/mp4:DLNA.ORG_PN=MPEG4_P2_MP4_SP_HEAAC,http-get:*:video/mp4:DLNA.ORG_PN=MPEG4_P2_MP4_SP_L2_AAC,http-get:*:video/mp4:DLNA.ORG_PN=MPEG4_P2_MP4_SP_VGA_AAC,http-get:*:video/mp4:DLNA.ORG_PN=MPEG4_P2_MP4_SP_VGA_HEAAC,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG4_P2_TS_SP_AC3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG4_P2_TS_SP_AC3_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG4_P2_TS_SP_MPEG1_L3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG4_P2_TS_SP_MPEG1_L3_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG4_P2_TS_SP_MPEG2_L2,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG4_P2_TS_SP_MPEG2_L2_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_KO,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_KO_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_KO_XAC3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_KO_XAC3_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_NA,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_NA_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_NA_XAC3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_HD_NA_XAC3_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_JP_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_MP_LL_AAC,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_MP_LL_AAC_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_EU,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_EU_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_KO,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_KO_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_KO_XAC3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_KO_XAC3_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_NA,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_NA_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_NA_XAC3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=MPEG_TS_SD_NA_XAC3_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF15_AAC,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF15_AAC_540,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF15_AAC_540_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF15_AAC_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF30_AAC_940,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF30_AAC_940_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF30_AAC_MULT5,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF30_AAC_MULT5_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF30_AC3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF30_AC3_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF30_MPEG1_L3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_BL_CIF30_MPEG1_L3_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_JP_AAC_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_AAC_MULT5,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_AAC_MULT5_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_AC3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_AC3_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_MPEG1_L3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_HD_MPEG1_L3_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_AAC_MULT5,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_AAC_MULT5_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_AC3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_AC3_T,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_MPEG1_L3,http-get:*:video/vnd.dlna.mpeg-tts:DLNA.ORG_PN=AVC_TS_MP_SD_MPEG1_L3_T";
    PLAYER_EVENT.fire("SinkProtocolInfo", (void*)value.c_str());
}

void DLNAActivity::onContentAvailable() {
    this->video->hideDLNAButton();
    this->video->hideDanmakuButton();
    this->video->hideVideoQualityButton();
    this->video->hideSubtitleSetting();
    this->video->hideVideoRelatedSetting();
    this->video->hideHistorySetting();
    this->video->hideHighlightLineSetting();
    this->video->hideSkipOpeningCreditsSetting();
    this->video->disableCloseOnEndOfFile();
    this->video->setFullscreenIcon(true);
    this->video->setTitle("wiliwili/setting/tools/others/dlna_waiting"_i18n);
    this->video->showOSD(false);
    this->video->setOnlineCount(fmt::format("http://{}:{}", ip, port));

    this->video->getFullscreenIcon()->getParent()->registerClickAction([this](...) {
        this->dismiss();
        return true;
    });
    this->video->registerAction(
        "cancel", brls::ControllerButton::BUTTON_B,
        [this](brls::View* view) -> bool {
            if (this->video->isOSDLock()) {
                this->video->toggleOSD();
            } else {
                this->dismiss();
            }
            return true;
        },
        true);

    // 手动将焦点 赋给video组件，这将允许焦点进入video组件内部
    brls::sync([this]() { brls::Application::giveFocus(video); });
}

void DLNAActivity::dismiss() {
    auto dialog = new brls::Dialog("wiliwili/setting/tools/others/dlna_quit"_i18n);
    dialog->addButton("hints/cancel"_i18n, []() {});
    dialog->addButton("hints/ok"_i18n, []() { brls::Application::popActivity(brls::TransitionAnimation::NONE); });
    dialog->open();
    brls::sync([dialog]() { brls::Application::giveFocus(dialog); });
}

DLNAActivity::~DLNAActivity() {
    MPV_E->unsubscribe(mpvEventSubscribeID);
    DLNA_EVENT.unsubscribe(dlnaEventSubscribeID);
    dlna->stop();
    video->stop();
}