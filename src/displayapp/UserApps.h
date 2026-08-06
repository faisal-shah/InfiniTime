#pragma once
#include "displayapp/apps/Apps.h"
#include "Controllers.h"

#include "displayapp/screens/MultiAlarm.h"
#include "displayapp/screens/ScheduleList.h"
#include "displayapp/screens/Tasks.h"
#include "displayapp/screens/PrayerTimes.h"
#include "displayapp/screens/Dice.h"
#include "displayapp/screens/Timer.h"
#include "displayapp/screens/Twos.h"
#include "displayapp/screens/Tile.h"
#include "displayapp/screens/ApplicationList.h"
#include "displayapp/screens/WatchFaceDigital.h"
#include "displayapp/screens/WatchFaceFamily.h"
#include "displayapp/screens/WatchFaceAnalog.h"
#include "displayapp/screens/WatchFaceCasioStyleG7710.h"
#include "displayapp/screens/WatchFaceInfineat.h"
#include "displayapp/screens/WatchFacePineTimeStyle.h"
#include "displayapp/screens/WatchFaceTerminal.h"
#include "displayapp/screens/WatchFacePrideFlag.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      class Screen;
    }

    struct AppDescription {
      Apps app;
      const char* icon;
      Screens::Screen* (*create)(AppControllers& controllers);
      bool (*isAvailable)(AppControllers& controllers);
    };

    struct WatchFaceDescription {
      WatchFace watchFace;
      const char* name;
      Screens::Screen* (*create)(AppControllers& controllers);
      bool (*isAvailable)(AppControllers& controllers);
    };

    template <Apps t>
    bool IsAppAvailable(AppControllers& controllers) {
      if constexpr (requires { AppTraits<t>::IsAvailable(controllers); }) {
        return AppTraits<t>::IsAvailable(controllers);
      } else {
        return AppTraits<t>::IsAvailable(controllers.filesystem);
      }
    }

    template <WatchFace t>
    bool IsWatchFaceAvailable(AppControllers& controllers) {
      if constexpr (requires { WatchFaceTraits<t>::IsAvailable(controllers); }) {
        return WatchFaceTraits<t>::IsAvailable(controllers);
      } else {
        return WatchFaceTraits<t>::IsAvailable(controllers.filesystem);
      }
    }

    template <Apps t>
    consteval AppDescription CreateAppDescription() {
      return {AppTraits<t>::app,
              AppTraits<t>::icon,
              &AppTraits<t>::Create,
              &IsAppAvailable<t>};
    }

    template <WatchFace t>
    consteval WatchFaceDescription CreateWatchFaceDescription() {
      return {WatchFaceTraits<t>::watchFace,
              WatchFaceTraits<t>::name,
              &WatchFaceTraits<t>::Create,
              &IsWatchFaceAvailable<t>};
    }

    template <template <Apps...> typename T, Apps... ts>
    consteval std::array<AppDescription, sizeof...(ts)> CreateAppDescriptions(T<ts...>) {
      return {CreateAppDescription<ts>()...};
    }

    template <template <WatchFace...> typename T, WatchFace... ts>
    consteval std::array<WatchFaceDescription, sizeof...(ts)> CreateWatchFaceDescriptions(T<ts...>) {
      return {CreateWatchFaceDescription<ts>()...};
    }

    constexpr auto userApps = CreateAppDescriptions(UserAppTypes {});
    constexpr auto userWatchFaces = CreateWatchFaceDescriptions(UserWatchFaceTypes {});
  }
}
