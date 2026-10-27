#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void ShowLauncherDialog();
int IsLauncherOpen();
void WaitForLauncherStartup(unsigned int milliseconds);
// A resolution that is not in the headset's ladder becomes the ladder entry nearest in cost; 1 when it
// changed. For the saved pick of a session that skips the dialog (src/Overlay/LauncherDialog.cpp).
int SnapLauncherResolution(int hmdType, int* width, int* height);

#ifdef __cplusplus
}
#endif
