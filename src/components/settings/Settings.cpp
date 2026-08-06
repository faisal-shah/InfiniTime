#include "components/settings/Settings.h"
#include "storagetask/StorageTask.h"

using namespace Pinetime::Controllers;

Settings::Settings(Pinetime::System::StorageTask& storageTask)
  : storageTask {storageTask} {
}

void Settings::Init() {
  LoadFromFamilyState();
}

void Settings::SaveSettings() {
  if (!settingsChanged) {
    return;
  }
  settingsGeneration++;
  saveRequested = true;
  lastSaveRequest = xTaskGetTickCount();
}

void Settings::Process() {
  if (!saveRequested || pendingToken != 0 ||
      xTaskGetTickCount() - lastSaveRequest < DebounceTicks ||
      storageTask.Busy()) {
    return;
  }
  const uint32_t token = nextToken++;
  if (nextToken == 0) {
    nextToken = 1;
  }
  if (!storageTask.BeginFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::Settings,
        token)) {
    return;
  }
  auto* candidate = storageTask.MutableCandidate(
    CompanionProtocol::FamilyStateOperation::Settings,
    token);
  if (candidate == nullptr) {
    storageTask.CancelFamilyStateMutation(
      CompanionProtocol::FamilyStateOperation::Settings,
      token);
    return;
  }
  ApplyToFamilyState(candidate->settings);
  pendingToken = token;
  submittedGeneration = settingsGeneration;
  if (!storageTask.CommitFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::Settings,
        token)) {
    pendingToken = 0;
    lastSaveRequest = xTaskGetTickCount();
  }
}

void Settings::OnPersisted(uint32_t token, bool success) {
  if (token != pendingToken) {
    return;
  }
  pendingToken = 0;
  if (!success) {
    lastSaveRequest = xTaskGetTickCount();
    saveRequested = true;
    return;
  }
  if (settingsGeneration == submittedGeneration) {
    settingsChanged = false;
    saveRequested = false;
    return;
  }
  lastSaveRequest = xTaskGetTickCount();
  saveRequested = true;
}

void Settings::LoadFromFamilyState() {
  const auto& input = storageTask.ActiveState().settings;
  settings.stepsGoal = input.stepsGoal;
  settings.screenTimeOut = input.screenTimeoutMs;
  settings.alwaysOnDisplay = input.alwaysOnDisplay;
  settings.clockType = static_cast<ClockType>(input.clockType);
  settings.weatherFormat = static_cast<WeatherFormat>(input.weatherFormat);
  settings.notificationStatus = static_cast<Notification>(input.notificationStatus);
  settings.watchFace = static_cast<Pinetime::Applications::WatchFace>(input.watchFace);
  settings.chimesOption = static_cast<ChimesOption>(input.chimeOption);
  settings.PTS.ColorTime = static_cast<Colors>(input.ptsColorTime);
  settings.PTS.ColorBar = static_cast<Colors>(input.ptsColorBar);
  settings.PTS.ColorBG = static_cast<Colors>(input.ptsColorBackground);
  settings.PTS.gaugeStyle = static_cast<PTSGaugeStyle>(input.ptsGaugeStyle);
  settings.PTS.weatherEnable = static_cast<PTSWeather>(input.ptsWeather);
  settings.prideFlag = static_cast<PrideFlag>(input.prideFlag);
  settings.watchFaceInfineat.showSideCover = input.infineatShowSideCover;
  settings.watchFaceInfineat.colorIndex = input.infineatColorIndex;
  settings.wakeUpMode = std::bitset<5> {input.wakeModes};
  settings.shakeWakeThreshold = input.shakeWakeThreshold;
  settings.brightLevel =
    static_cast<Controllers::BrightnessController::Levels>(input.brightness);
  settings.dfuAndFsEnabledOnBoot = input.dfuAndFsEnabledOnBoot;
  settings.heartRateBackgroundPeriod = input.heartRateBackgroundPeriod;
}

void Settings::ApplyToFamilyState(FamilyState::Settings& output) const {
  output.stepsGoal = settings.stepsGoal;
  output.screenTimeoutMs = settings.screenTimeOut;
  output.infineatColorIndex = settings.watchFaceInfineat.colorIndex;
  output.shakeWakeThreshold = settings.shakeWakeThreshold;
  output.heartRateBackgroundPeriod = settings.heartRateBackgroundPeriod;
  output.watchFace = static_cast<uint8_t>(settings.watchFace);
  output.clockType = static_cast<uint8_t>(settings.clockType);
  output.weatherFormat = static_cast<uint8_t>(settings.weatherFormat);
  output.notificationStatus = static_cast<uint8_t>(settings.notificationStatus);
  output.chimeOption = static_cast<uint8_t>(settings.chimesOption);
  output.brightness = static_cast<uint8_t>(settings.brightLevel);
  output.wakeModes = static_cast<uint8_t>(settings.wakeUpMode.to_ulong());
  output.ptsColorTime = static_cast<uint8_t>(settings.PTS.ColorTime);
  output.ptsColorBar = static_cast<uint8_t>(settings.PTS.ColorBar);
  output.ptsColorBackground = static_cast<uint8_t>(settings.PTS.ColorBG);
  output.ptsGaugeStyle = static_cast<uint8_t>(settings.PTS.gaugeStyle);
  output.ptsWeather = static_cast<uint8_t>(settings.PTS.weatherEnable);
  output.prideFlag = static_cast<uint8_t>(settings.prideFlag);
  output.alwaysOnDisplay = settings.alwaysOnDisplay;
  output.infineatShowSideCover = settings.watchFaceInfineat.showSideCover;
  output.dfuAndFsEnabledOnBoot = settings.dfuAndFsEnabledOnBoot;
}
