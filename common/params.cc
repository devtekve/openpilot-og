#include "common/params.h"

#include <dirent.h>
#include <sys/file.h>

#include <algorithm>
#include <csignal>
#include <unordered_map>

#include "common/swaglog.h"
#include "common/util.h"
#include "system/hardware/hw.h"

namespace {

volatile sig_atomic_t params_do_exit = 0;
void params_sig_handler(int signal) {
  params_do_exit = 1;
}

int fsync_dir(const std::string &path) {
  int result = -1;
  int fd = HANDLE_EINTR(open(path.c_str(), O_RDONLY, 0755));
  if (fd >= 0) {
    result = fsync(fd);
    close(fd);
  }
  return result;
}

bool create_params_path(const std::string &param_path, const std::string &key_path) {
  // Make sure params path exists
  if (!util::file_exists(param_path) && !util::create_directories(param_path, 0775)) {
    return false;
  }

  // See if the symlink exists, otherwise create it
  if (!util::file_exists(key_path)) {
    // 1) Create temp folder
    // 2) Symlink it to temp link
    // 3) Move symlink to <params>/d

    std::string tmp_path = param_path + "/.tmp_XXXXXX";
    // this should be OK since mkdtemp just replaces characters in place
    char *tmp_dir = mkdtemp((char *)tmp_path.c_str());
    if (tmp_dir == NULL) {
      return false;
    }

    std::string link_path = std::string(tmp_dir) + ".link";
    if (symlink(tmp_dir, link_path.c_str()) != 0) {
      return false;
    }

    // don't return false if it has been created by other
    if (rename(link_path.c_str(), key_path.c_str()) != 0 && errno != EEXIST) {
      return false;
    }
  }

  return true;
}

std::string ensure_params_path(const std::string &prefix, const std::string &path = {}) {
  std::string params_path = path.empty() ? Path::params() : path;
  if (create_params_path(params_path, params_path + prefix)) {
   return params_path;
  }
  else if (create_params_path("~" + params_path, "~" + params_path + prefix))  {
     return "~" + params_path;
  }

   throw std::runtime_error(util::string_format(
          "Failed to ensure params path, errno=%d, path=%s, param_prefix=%s",
          errno, params_path.c_str(), prefix.c_str()));

}

class FileLock {
public:
  FileLock(const std::string &fn) {
    fd_ = HANDLE_EINTR(open(fn.c_str(), O_CREAT, 0775));
    if (fd_ < 0 || HANDLE_EINTR(flock(fd_, LOCK_EX)) < 0) {
      LOGE("Failed to lock file %s, errno=%d", fn.c_str(), errno);
    }
  }
  ~FileLock() { close(fd_); }

private:
  int fd_ = -1;
};

std::unordered_map<std::string, uint32_t> keys = {
    {"AccessToken", CLEAR_ON_MANAGER_START | DONT_LOG},
    {"ApiCache_Device", PERSISTENT},
    {"ApiCache_DriveStats", PERSISTENT},
    {"ApiCache_NavDestinations", PERSISTENT},
    {"AssistNowToken", PERSISTENT},
    {"AthenadPid", PERSISTENT},
    {"AthenadUploadQueue", PERSISTENT},
    {"CalibrationParams", PERSISTENT},
    {"CameraDebugExpGain", CLEAR_ON_MANAGER_START},
    {"CameraDebugExpTime", CLEAR_ON_MANAGER_START},
    {"CarBatteryCapacity", PERSISTENT},
    {"CarParams", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"CarParamsCache", CLEAR_ON_MANAGER_START},
    {"CarParamsPersistent", PERSISTENT},
    {"CarVin", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"CompletedTrainingVersion", PERSISTENT},
    {"ControlsReady", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"CurrentBootlog", PERSISTENT},
    {"CurrentRoute", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"DisableLogging", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"DisablePowerDown", PERSISTENT},
    {"DisableUpdates", PERSISTENT},
    {"DisengageOnAccelerator", PERSISTENT},
    {"DmModelInitialized", CLEAR_ON_ONROAD_TRANSITION},
    {"DongleId", PERSISTENT}, // NO LONGER PRESENT
    {"DoReboot", CLEAR_ON_MANAGER_START}, // NO LONGER PRESENT
    {"DoShutdown", CLEAR_ON_MANAGER_START},
    {"DoUninstall", CLEAR_ON_MANAGER_START},
    {"ExperimentalLongitudinalEnabled", PERSISTENT},
    {"ExperimentalMode", PERSISTENT},
    {"ExperimentalModeConfirmed", PERSISTENT},
    {"FirmwareQueryDone", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"ForcePowerDown", PERSISTENT},
    {"GitBranch", PERSISTENT},
    {"GitCommit", PERSISTENT},
    {"GitDiff", PERSISTENT},
    {"GithubSshKeys", PERSISTENT},
    {"GithubUsername", PERSISTENT},
    {"GitRemote", PERSISTENT},
    {"GsmApn", PERSISTENT},
    {"GsmMetered", PERSISTENT},
    {"GsmRoaming", PERSISTENT},
    {"HardwareSerial", PERSISTENT},
    {"HasAcceptedTerms", PERSISTENT},
    {"IMEI", PERSISTENT}, // NO LONGER PRESENT
    {"InstallDate", PERSISTENT},
    {"IsDriverViewEnabled", CLEAR_ON_MANAGER_START},
    {"IsEngaged", PERSISTENT},
    {"IsLdwEnabled", PERSISTENT},
    {"IsMetric", PERSISTENT}, // NO LONGER PRESENT
    {"IsOffroad", CLEAR_ON_MANAGER_START},
    {"IsOnroad", PERSISTENT}, // NO LONGER PRESENT
    {"IsRhdDetected", PERSISTENT},
    {"IsReleaseBranch", CLEAR_ON_MANAGER_START},
    {"IsTakingSnapshot", CLEAR_ON_MANAGER_START}, // NO LONGER PRESENT
    {"IsTestedBranch", CLEAR_ON_MANAGER_START},
    {"IsUpdateAvailable", CLEAR_ON_MANAGER_START},
    {"JoystickDebugMode", CLEAR_ON_MANAGER_START | CLEAR_ON_OFFROAD_TRANSITION},
    {"LaikadEphemerisV3", PERSISTENT | DONT_LOG},
    {"LanguageSetting", PERSISTENT},
    {"LastAthenaPingTime", CLEAR_ON_MANAGER_START},
    {"LastGPSPosition", PERSISTENT},
    {"LastManagerExitReason", CLEAR_ON_MANAGER_START},
    {"LastOffroadStatusPacket", CLEAR_ON_MANAGER_START | CLEAR_ON_OFFROAD_TRANSITION},
    {"LastPowerDropDetected", CLEAR_ON_MANAGER_START},
    {"LastSystemShutdown", CLEAR_ON_MANAGER_START},
    {"LastUpdateException", CLEAR_ON_MANAGER_START},
    {"LastUpdateTime", PERSISTENT},
    {"LiveParameters", PERSISTENT},
    {"LiveTorqueCarParams", PERSISTENT},
    {"LiveTorqueParameters", PERSISTENT | DONT_LOG},
    {"LongitudinalPersonality", PERSISTENT},
    {"NavDestination", CLEAR_ON_MANAGER_START | CLEAR_ON_OFFROAD_TRANSITION},
    {"NavDestinationWaypoints", CLEAR_ON_MANAGER_START | CLEAR_ON_OFFROAD_TRANSITION},
    {"NavPastDestinations", PERSISTENT},
    {"NavSettingLeftSide", PERSISTENT},
    {"NavSettingTime24h", PERSISTENT},
    {"NavdRender", PERSISTENT},
    {"ObdMultiplexingChanged", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"ObdMultiplexingEnabled", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"Offroad_BadNvme", CLEAR_ON_MANAGER_START},
    {"Offroad_CarUnrecognized", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"Offroad_ConnectivityNeeded", CLEAR_ON_MANAGER_START},
    {"Offroad_ConnectivityNeededPrompt", CLEAR_ON_MANAGER_START},
    {"Offroad_InvalidTime", CLEAR_ON_MANAGER_START},
    {"Offroad_IsTakingSnapshot", CLEAR_ON_MANAGER_START},
    {"Offroad_NeosUpdate", CLEAR_ON_MANAGER_START},
    {"Offroad_NoFirmware", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"Offroad_Recalibration", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"Offroad_StorageMissing", CLEAR_ON_MANAGER_START},
    {"Offroad_TemperatureTooHigh", CLEAR_ON_MANAGER_START},
    {"Offroad_UnofficialHardware", CLEAR_ON_MANAGER_START},
    {"Offroad_UpdateFailed", CLEAR_ON_MANAGER_START},
    {"OpenpilotEnabledToggle", PERSISTENT},
    {"PandaHeartbeatLost", CLEAR_ON_MANAGER_START | CLEAR_ON_OFFROAD_TRANSITION},
    {"PandaLogState", PERSISTENT},
    {"PandaSignatures", CLEAR_ON_MANAGER_START},
    {"Passive", PERSISTENT},
    {"PrimeType", PERSISTENT},
    {"RecordFront", PERSISTENT},
    {"RecordFrontLock", PERSISTENT},  // for the internal fleet
    {"ReplayControlsState", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION},
    {"ShouldDoUpdate", CLEAR_ON_MANAGER_START},
    {"SnoozeUpdate", CLEAR_ON_MANAGER_START | CLEAR_ON_OFFROAD_TRANSITION},
    {"SshEnabled", PERSISTENT},
    {"SubscriberInfo", PERSISTENT},
    {"TermsVersion", PERSISTENT},
    {"Timezone", PERSISTENT}, // NO LONGER PRESENT
    {"TrainingVersion", PERSISTENT}, // NO LONGER PRESENT
    {"UbloxAvailable", PERSISTENT},
    {"UpdateAvailable", CLEAR_ON_MANAGER_START | CLEAR_ON_ONROAD_TRANSITION}, // NO LONGER PRESENT
    {"UpdateFailedCount", CLEAR_ON_MANAGER_START},
    {"UpdaterAvailableBranches", CLEAR_ON_MANAGER_START},
    {"UpdaterCurrentDescription", CLEAR_ON_MANAGER_START},
    {"UpdaterCurrentReleaseNotes", CLEAR_ON_MANAGER_START},
    {"UpdaterFetchAvailable", CLEAR_ON_MANAGER_START},
    {"UpdaterNewDescription", CLEAR_ON_MANAGER_START},
    {"UpdaterNewReleaseNotes", CLEAR_ON_MANAGER_START},
    {"UpdaterState", CLEAR_ON_MANAGER_START},
    {"UpdaterTargetBranch", CLEAR_ON_MANAGER_START},
    {"Version", PERSISTENT}, // NO LONGER PRESENT
    {"VisionRadarToggle", PERSISTENT},
    {"WheeledBody", PERSISTENT},

    {"AccMadsCombo", CLEAR_ON_MANAGER_START},
    {"AutoLaneChangeTimer", CLEAR_ON_MANAGER_START},
    {"AutoLaneChangeBsmDelay", CLEAR_ON_MANAGER_START},
    {"BelowSpeedPause", CLEAR_ON_MANAGER_START},
    {"BrakeLights", CLEAR_ON_MANAGER_START},
    {"BrightnessControl", CLEAR_ON_MANAGER_START},
    {"CustomTorqueLateral", CLEAR_ON_MANAGER_START},
    {"CameraControl", CLEAR_ON_MANAGER_START},
    {"CameraControlToggle", CLEAR_ON_MANAGER_START},
    {"CameraOffset", CLEAR_ON_MANAGER_START},
    {"CarModel", CLEAR_ON_MANAGER_START}, // NO LONGER PRESENT
    {"CarModelText", CLEAR_ON_MANAGER_START},
    {"ChevronInfo", CLEAR_ON_MANAGER_START},
    {"MadsCruiseMain", CLEAR_ON_MANAGER_START},
    {"CustomBootScreen", CLEAR_ON_MANAGER_START},
    {"CustomOffsets", CLEAR_ON_MANAGER_START},
    {"DevUI", CLEAR_ON_MANAGER_START}, // NO LONGER PRESENT
    {"DevUIInfo", CLEAR_ON_MANAGER_START},
    {"DisableOnroadUploads", CLEAR_ON_MANAGER_START},
    {"DisengageLateralOnBrake", CLEAR_ON_MANAGER_START},
    {"DynamicLaneProfile", CLEAR_ON_MANAGER_START},
    {"DynamicLaneProfileToggle", CLEAR_ON_MANAGER_START},
    {"EnableMads", CLEAR_ON_MANAGER_START},
    {"EnhancedScc", CLEAR_ON_MANAGER_START},
    {"GapAdjustCruise", CLEAR_ON_MANAGER_START},
    {"GapAdjustCruiseMax", CLEAR_ON_MANAGER_START},
    {"GapAdjustCruiseMin", CLEAR_ON_MANAGER_START},
    {"GapAdjustCruiseMode", CLEAR_ON_MANAGER_START},
    {"GapAdjustCruiseTr", CLEAR_ON_MANAGER_START},
    {"GpxDeleteAfterUpload", CLEAR_ON_MANAGER_START},
    {"GpxDeleteIfUploaded", CLEAR_ON_MANAGER_START},
    {"HandsOnWheelMonitoring", CLEAR_ON_MANAGER_START},
    {"HideVEgoUi", CLEAR_ON_MANAGER_START},
    {"LastSpeedLimitSignTap", CLEAR_ON_MANAGER_START},
    {"LkasToggle", CLEAR_ON_MANAGER_START},
    {"MadsIconToggle", CLEAR_ON_MANAGER_START},
    {"MaxTimeOffroad", CLEAR_ON_MANAGER_START},
    {"OnroadScreenOff", CLEAR_ON_MANAGER_START},
    {"OnroadScreenOffBrightness", CLEAR_ON_MANAGER_START},
    {"OnroadScreenOffEvent", CLEAR_ON_MANAGER_START},
    {"PathOffset", CLEAR_ON_MANAGER_START},
    {"ReverseAccChange", CLEAR_ON_MANAGER_START},
    {"ScreenRecorder", CLEAR_ON_MANAGER_START},
    {"ShowDebugUI", CLEAR_ON_MANAGER_START},
    {"SpeedLimitControl", CLEAR_ON_MANAGER_START},
    {"SpeedLimitPercOffset", CLEAR_ON_MANAGER_START},
    {"SpeedLimitValueOffset", CLEAR_ON_MANAGER_START},
    {"SpeedLimitOffsetType", CLEAR_ON_MANAGER_START},
    {"StandStillTimer", CLEAR_ON_MANAGER_START},
    {"StockLongToyota", CLEAR_ON_MANAGER_START},
    {"TorqueDeadzoneDeg", CLEAR_ON_MANAGER_START},
    {"TorqueFriction", CLEAR_ON_MANAGER_START},
    {"TorqueMaxLatAccel", CLEAR_ON_MANAGER_START},
    {"TrueVEgoUi", CLEAR_ON_MANAGER_START},
    {"TurnSpeedControl", CLEAR_ON_MANAGER_START},
    {"TurnVisionControl", CLEAR_ON_MANAGER_START},
    {"VisionCurveLaneless", CLEAR_ON_MANAGER_START},
    {"VwAccType", CLEAR_ON_MANAGER_START},
    {"HotspotOnBoot", CLEAR_ON_MANAGER_START},
    {"DriverCameraHardwareMissing", CLEAR_ON_MANAGER_START},
    {"CustomMapbox", CLEAR_ON_MANAGER_START},
    {"OsmLocalDb", CLEAR_ON_MANAGER_START},
    {"HkgSmoothStop", CLEAR_ON_MANAGER_START},
    {"EnforceTorqueLateral", CLEAR_ON_MANAGER_START},
    {"OsmWayTest", CLEAR_ON_MANAGER_START},
    {"ReverseDmCam", CLEAR_ON_MANAGER_START},
    {"CustomStockLong", CLEAR_ON_MANAGER_START},
    {"RoadEdge", CLEAR_ON_MANAGER_START},
    {"LiveTorque", CLEAR_ON_MANAGER_START},
    {"DynamicLongitudinalControl", CLEAR_ON_MANAGER_START},
    {"ButtonAutoHide", PERSISTENT}, // NEWLY DISCOVERED
    {"CustomMapboxTokenPk", PERSISTENT}, // NEWLY DISCOVERED
    {"CustomMapboxTokenSk", PERSISTENT}, // NEWLY DISCOVERED
    {"DynamicLongitudinalControlToggle", PERSISTENT}, // NEWLY DISCOVERED
    {"EnableAmap", PERSISTENT}, // NEWLY DISCOVERED
    {"EnableDebugSnapshot", PERSISTENT}, // NEWLY DISCOVERED
    {"EnableGmap", PERSISTENT}, // NEWLY DISCOVERED
    {"EndToEndLongAlertLead", PERSISTENT}, // NEWLY DISCOVERED
    {"EndToEndLongAlertLight", PERSISTENT}, // NEWLY DISCOVERED
    {"EndToEndLongAlertUI", PERSISTENT}, // NEWLY DISCOVERED
    {"EndToEndLongToggle", PERSISTENT}, // NEWLY DISCOVERED
    {"FleetManagerPin", PERSISTENT}, // NEWLY DISCOVERED
    {"MapboxFullScreen", PERSISTENT}, // NEWLY DISCOVERED
    {"Offroad_SupersededUpdate", PERSISTENT}, // NEWLY DISCOVERED
    {"OsmDbUpdatesCheck", PERSISTENT}, // NEWLY DISCOVERED
    {"OsmLocationName", PERSISTENT}, // NEWLY DISCOVERED
    {"OsmLocationTitle", PERSISTENT}, // NEWLY DISCOVERED
    {"OsmLocationUrl", PERSISTENT}, // NEWLY DISCOVERED
    {"QuietDrive", PERSISTENT}, // NEWLY DISCOVERED
    {"SidebarTemperature", PERSISTENT}, // NEWLY DISCOVERED
    {"SidebarTemperatureOptions", PERSISTENT}, // NEWLY DISCOVERED
    {"ToyotaTSS2Long", PERSISTENT}, // NEWLY DISCOVERED
    {"UpdateFailedCount", PERSISTENT}, // NEWLY DISCOVERED
    {"LastCarModel", PERSISTENT}, // NEWLY DISCOVERED
};

} // namespace


Params::Params(const std::string &path) {
  prefix = "/" + util::getenv("OPENPILOT_PREFIX", "d");
  params_path = ensure_params_path(prefix, path);
}

std::vector<std::string> Params::allKeys() const {
  std::vector<std::string> ret;
  for (auto &p : keys) {
    ret.push_back(p.first);
  }
  return ret;
}

bool Params::checkKey(const std::string &key) {
  return keys.find(key) != keys.end();
}

ParamKeyType Params::getKeyType(const std::string &key) {
  return static_cast<ParamKeyType>(keys[key]);
}

int Params::put(const char* key, const char* value, size_t value_size) {
  // Information about safely and atomically writing a file: https://lwn.net/Articles/457667/
  // 1) Create temp file
  // 2) Write data to temp file
  // 3) fsync() the temp file
  // 4) rename the temp file to the real name
  // 5) fsync() the containing directory
  std::string tmp_path = params_path + "/.tmp_value_XXXXXX";
  int tmp_fd = mkstemp((char*)tmp_path.c_str());
  if (tmp_fd < 0) return -1;

  int result = -1;
  do {
    // Write value to temp.
    ssize_t bytes_written = HANDLE_EINTR(write(tmp_fd, value, value_size));
    if (bytes_written < 0 || (size_t)bytes_written != value_size) {
      result = -20;
      break;
    }

    // fsync to force persist the changes.
    if ((result = fsync(tmp_fd)) < 0) break;

    FileLock file_lock(params_path + "/.lock");

    // Move temp into place.
    if ((result = rename(tmp_path.c_str(), getParamPath(key).c_str())) < 0) break;

    // fsync parent directory
    result = fsync_dir(getParamPath());
  } while (false);

  close(tmp_fd);
  ::unlink(tmp_path.c_str());
  return result;
}

int Params::remove(const std::string &key) {
  FileLock file_lock(params_path + "/.lock");
  int result = unlink(getParamPath(key).c_str());
  if (result != 0) {
    return result;
  }
  return fsync_dir(getParamPath());
}

std::string Params::get(const std::string &key, bool block) {
  if (!block) {
    return util::read_file(getParamPath(key));
  } else {
    // blocking read until successful
    params_do_exit = 0;
    void (*prev_handler_sigint)(int) = std::signal(SIGINT, params_sig_handler);
    void (*prev_handler_sigterm)(int) = std::signal(SIGTERM, params_sig_handler);

    std::string value;
    while (!params_do_exit) {
      if (value = util::read_file(getParamPath(key)); !value.empty()) {
        break;
      }
      util::sleep_for(100);  // 0.1 s
    }

    std::signal(SIGINT, prev_handler_sigint);
    std::signal(SIGTERM, prev_handler_sigterm);
    return value;
  }
}

std::map<std::string, std::string> Params::readAll() {
  FileLock file_lock(params_path + "/.lock");
  return util::read_files_in_dir(getParamPath());
}

void Params::clearAll(ParamKeyType key_type) {
  FileLock file_lock(params_path + "/.lock");

  // 1) delete params of key_type
  // 2) delete files that are not defined in the keys.
  if (DIR *d = opendir(getParamPath().c_str())) {
    struct dirent *de = NULL;
    while ((de = readdir(d))) {
      if (de->d_type != DT_DIR) {
        auto it = keys.find(de->d_name);
        if (it == keys.end() || (it->second & key_type)) {
          unlink(getParamPath(de->d_name).c_str());
        }
      }
    }
    closedir(d);
  }

  fsync_dir(getParamPath());
}
