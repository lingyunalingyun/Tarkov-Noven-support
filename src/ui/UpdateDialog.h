#pragma once
#include "updates/UpdateService.h"
#include <windows.h>
namespace noven::ui {bool ShowUpdateDialog(HWND owner,updates::UpdateService& service);}
