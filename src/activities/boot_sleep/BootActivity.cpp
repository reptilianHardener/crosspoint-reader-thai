#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  //const int logoX = (pageWidth - BOOT_LOGO_WIDTH) / 2;
  //const int logoY = (pageHeight - BOOT_LOGO_HEIGHT) / 2;
  //constexpr int kLogoToTitleGap = 20;
  //constexpr int kTitleToSubtitleGap = 25;
  //const int titleY = logoY + BOOT_LOGO_HEIGHT + kLogoToTitleGap;
  //renderer.drawImage(BootLogo, logoX, logoY, BOOT_LOGO_WIDTH, BOOT_LOGO_HEIGHT);
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
  //renderer.drawCenteredText(UI_10_FONT_ID, titleY, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  //renderer.drawCenteredText(SMALL_FONT_ID, titleY + kTitleToSubtitleGap, tr(STR_BOOTING));
  //renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 50, tr(STR_BOOT_CREDIT));
  //renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  //renderer.displayBuffer();
}
