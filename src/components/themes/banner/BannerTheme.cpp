#include "BannerTheme.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
constexpr int kSidePadding = 16;
constexpr int kSectionGap = 8;
constexpr int kCardRadius = 18;
constexpr int kFrameWidth = 2;
constexpr int kBoxPadding = 10;  // covers box: inner padding around the three slots
constexpr int kSlotGap = 10;
constexpr int kSelectionInset = 4;  // selection ring sits this far outside the cover
// Image areas, drawn 1:1 when the image is exactly this size.
constexpr int kTopImageWidth = 440;
constexpr int kTopImageHeight = 110;
constexpr int kMiddleImageWidth = 440;
constexpr int kMiddleImageHeight = 190;
constexpr int kImageInset = 4;  // middle image sits inside its frame
constexpr int kMinBannerHeight = 40;
constexpr int kMinCoverHeight = 100;
constexpr int kCardPadding = 12;
constexpr int kBarHeight = 10;
constexpr int kBarGap = 8;
constexpr int kMenuHeight = 56;
constexpr int kMenuBottomGap = 6;
constexpr int kIconSize = 32;
constexpr int kIconHighlight = 48;
constexpr int kCoverCount = 3;

struct TileLayout {
  Rect top;
  Rect covers;
  Rect middle;
  Rect card;
  int slotWidth = 0;
  int coverHeight = 0;
};

// Top of the menu row: right under the tile HomeActivity reserves, pulled up when the
// screen is shorter than the portrait layout (landscape).
int menuTop(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int belowTile = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int aboveHints = renderer.getScreenHeight() - metrics.buttonHintsHeight - kMenuBottomGap - kMenuHeight;
  return std::min(belowTile, aboveHints);
}

int cardHeight(const GfxRenderer& renderer) {
  return kCardPadding + renderer.getLineHeight(UI_12_FONT_ID) + 2 + renderer.getLineHeight(SMALL_FONT_ID) + kBarGap +
         kBarHeight + kCardPadding;
}

// The images keep their full size and the covers take the height that is left, up to
// a 2:3 slot. Only when even the smallest covers would not fit (landscape) do the images
// shrink, and an image shorter than kMinBannerHeight is dropped. Leftover height is
// spread over the gaps between the sections.
TileLayout computeLayout(const GfxRenderer& renderer, const Rect rect, const bool withCard, const bool withTop,
                         const bool withMiddle) {
  TileLayout l;
  const int x = rect.x + kSidePadding;
  const int width = rect.width - 2 * kSidePadding;
  const int top = rect.y;
  const int bottom = std::min(rect.y + rect.height, menuTop(renderer) - kSectionGap);
  const int available = std::max(0, bottom - top);

  const int cardH = withCard ? cardHeight(renderer) : 0;
  const int cardSpace = withCard ? cardH + kSectionGap : 0;
  l.slotWidth = (width - 2 * kBoxPadding - (kCoverCount - 1) * kSlotGap) / kCoverCount;
  const int maxCoverH = l.slotWidth * 3 / 2;

  int topH = withTop ? kTopImageHeight : 0;
  int middleH = withMiddle ? kMiddleImageHeight + 2 * kImageInset : 0;
  const int bannerRoom = available - (kMinCoverHeight + 2 * kBoxPadding) - cardSpace - (withTop ? kSectionGap : 0) -
                         (withMiddle ? kSectionGap : 0);
  if (topH + middleH > bannerRoom) {
    const int room = std::max(0, bannerRoom);
    topH = topH * room / (topH + middleH);
    middleH = room - topH;
    if (topH < kMinBannerHeight) topH = 0;
    if (middleH < kMinBannerHeight) middleH = 0;
  }
  const int bannerSpace = (topH > 0 ? topH + kSectionGap : 0) + (middleH > 0 ? middleH + kSectionGap : 0);
  l.coverHeight =
      std::max(kMinCoverHeight, std::min(maxCoverH, available - cardSpace - bannerSpace - 2 * kBoxPadding));
  const int coversH = l.coverHeight + 2 * kBoxPadding;

  const int sections = 1 + (withCard ? 1 : 0) + (topH > 0 ? 1 : 0) + (middleH > 0 ? 1 : 0);
  const int used = coversH + cardH + topH + middleH + (sections - 1) * kSectionGap;
  const int gap = kSectionGap + (sections > 1 ? std::max(0, available - used) / (sections - 1) : 0);

  int y = top;
  if (topH > 0) {
    const int topW = std::min(width, kTopImageWidth);
    l.top = Rect{x + (width - topW) / 2, y, topW, topH};
    y += topH + gap;
  }
  l.covers = Rect{x, y, width, coversH};
  y += coversH + gap;
  if (middleH > 0) {
    const int middleW = std::min(width, kMiddleImageWidth + 2 * kImageInset);
    l.middle = Rect{x + (width - middleW) / 2, y, middleW, middleH};
    y += middleH + gap;
  }
  if (withCard) l.card = Rect{x, y, width, cardH};
  return l;
}

Rect tileRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return Rect{0, metrics.homeTopPadding, renderer.getScreenWidth(), metrics.homeCoverTileHeight};
}

Rect slotRect(const TileLayout& l, const int i) {
  return Rect{l.covers.x + kBoxPadding + i * (l.slotWidth + kSlotGap), l.covers.y + kBoxPadding, l.slotWidth,
              l.coverHeight};
}

constexpr char kCacheDir[] = "/.crosspoint";
constexpr char kCachePrefix[] = "banner_";

// Removes converted banners in kCacheDir that `stale` selects by file name.
template <typename Stale>
void removeConvertedBanners(Stale&& stale) {
  std::vector<std::string> doomed;
  {
    HalFile dir = Storage.open(kCacheDir);
    if (!dir || !dir.isDirectory()) return;
    char name[64];
    for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      if (entry.isDirectory()) continue;
      entry.getName(name, sizeof(name));
      if (strncmp(name, kCachePrefix, sizeof(kCachePrefix) - 1) == 0 && stale(name)) {
        doomed.emplace_back(std::string(kCacheDir) + "/" + name);
      }
    }
  }
  for (const auto& path : doomed) {
    if (!Storage.remove(path.c_str())) LOG_ERR("HOME", "Could not remove %s", path.c_str());
  }
}

// banner_<8 hex>_... : the 2.0.0.1 naming, which recorded no slot.
bool isUnslottedBanner(const char* name) {
  const char* p = name + sizeof(kCachePrefix) - 1;
  for (int i = 0; i < 8; i++) {
    if (!isxdigit(static_cast<unsigned char>(p[i]))) return false;
  }
  return p[8] == '_';
}

// A JPG banner is decoded once into a BMP sized for its box under /.crosspoint, named
// banner_<slot>_<hash>_<w>x<h>.bmp. The hash keys on the source path and size, so a
// replaced image converts again; conversions of the slot's earlier images are deleted.
bool jpgBannerBmpPath(const char* slot, const char* jpgPath, const Rect box, std::string& bmpPath) {
  HalFile jpg;
  if (!Storage.openFileForRead("HOME", jpgPath, jpg)) return false;
  const std::string key = std::string(jpgPath) + ":" + std::to_string(jpg.fileSize());
  const unsigned hash = static_cast<unsigned>(std::hash<std::string>{}(key));
  char name[80];
  snprintf(name, sizeof(name), "%s/%s%s_%08x_%dx%d.bmp", kCacheDir, kCachePrefix, slot, hash, box.width, box.height);
  bmpPath = name;
  if (Storage.exists(name)) return true;

  char keep[32];  // this image's prefix: its other box sizes (orientations) stay
  snprintf(keep, sizeof(keep), "%s%s_%08x_", kCachePrefix, slot, hash);
  char slotPrefix[24];
  snprintf(slotPrefix, sizeof(slotPrefix), "%s%s_", kCachePrefix, slot);
  removeConvertedBanners([&](const char* file) {
    if (isUnslottedBanner(file)) return true;
    return strncmp(file, slotPrefix, strlen(slotPrefix)) == 0 && strncmp(file, keep, strlen(keep)) != 0;
  });

  HalFile bmp;
  if (!Storage.openFileForWrite("HOME", bmpPath, bmp)) return false;
  const bool ok = JpegToBmpConverter::jpegFileToBmpStreamWithSize(jpg, bmp, box.width, box.height);
  // Close before removing a failed output.
  bmp.close();
  if (!ok) {
    LOG_ERR("HOME", "Failed to convert banner image: %s", jpgPath);
    Storage.remove(name);
  }
  return ok;
}

// Draws a user BMP or JPG centred in `box`, scaled down to fit (never up). Returns the
// drawn rect, or an empty one when the file is missing or unreadable.
Rect drawImageFitted(const GfxRenderer& renderer, const char* slot, const char* path, const Rect box) {
  if (path[0] == '\0' || box.width <= 0 || box.height <= 0) return Rect{};
  std::string bmpPath = path;
  if (FsHelpers::hasJpgExtension(std::string_view{path}) && !jpgBannerBmpPath(slot, path, box, bmpPath)) {
    return Rect{};
  }
  HalFile file;
  if (!Storage.openFileForRead("HOME", bmpPath, file)) return Rect{};
  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    LOG_ERR("HOME", "Unreadable banner image: %s", path);
    return Rect{};
  }
  const float scale = std::min({1.0f, static_cast<float>(box.width) / static_cast<float>(bitmap.getWidth()),
                                static_cast<float>(box.height) / static_cast<float>(bitmap.getHeight())});
  const int w = static_cast<int>(static_cast<float>(bitmap.getWidth()) * scale);
  const int h = static_cast<int>(static_cast<float>(bitmap.getHeight()) * scale);
  const Rect drawn{box.x + (box.width - w) / 2, box.y + (box.height - h) / 2, w, h};
  renderer.drawBitmap(bitmap, drawn.x, drawn.y, box.width, box.height);
  return drawn;
}

void drawCovers(GfxRenderer& renderer, const TileLayout& l, const std::vector<RecentBook>& recentBooks,
                const int thumbHeight) {
  renderer.drawRoundedRect(l.covers.x, l.covers.y, l.covers.width, l.covers.height, kFrameWidth, kCardRadius, true);
  if (recentBooks.empty()) {
    const int textY = l.covers.y + (l.covers.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    const std::string text =
        renderer.truncatedText(UI_12_FONT_ID, tr(STR_NO_OPEN_BOOK), l.covers.width - 2 * kBoxPadding);
    const int textX = l.covers.x + (l.covers.width - renderer.getTextWidth(UI_12_FONT_ID, text.c_str())) / 2;
    renderer.drawText(UI_12_FONT_ID, textX, textY, text.c_str(), true);
    return;
  }

  const int count = std::min(static_cast<int>(recentBooks.size()), kCoverCount);
  for (int i = 0; i < count; i++) {
    const Rect slot = slotRect(l, i);
    bool hasCover = false;
    if (!recentBooks[i].coverBmpPath.empty()) {
      HalFile file;
      const std::string thumbPath = UITheme::getCoverThumbPath(recentBooks[i].coverBmpPath, thumbHeight);
      if (Storage.openFileForRead("HOME", thumbPath, file)) {
        Bitmap bitmap(file);
        hasCover = bitmap.parseHeaders() == BmpReaderError::Ok && BaseTheme::drawCoverThumbFill(renderer, bitmap, slot);
      }
    }
    if (!hasCover) {
      renderer.fillRect(slot.x, slot.y + slot.height / 3, slot.width, 2 * slot.height / 3, true);
      renderer.drawIcon(CoverIcon, slot.x + 16, slot.y + 16, 32);
    }
    renderer.drawRect(slot.x, slot.y, slot.width, slot.height, true);
  }
}

void drawBar(const GfxRenderer& renderer, const int x, const int y, const int width, const int percent) {
  renderer.drawRoundedRect(x, y, width, kBarHeight, 1, kBarHeight / 2, true);
  const int fill = (width - 4) * std::clamp(percent, 0, 100) / 100;
  if (fill > 0) renderer.fillRoundedRect(x + 2, y + 2, fill, kBarHeight - 4, (kBarHeight - 4) / 2, Color::Black);
}

// Drawn on every frame (outside the cached snapshot): it follows the highlighted cover.
void drawBookCard(const GfxRenderer& renderer, const Rect card, const RecentBook& book) {
  renderer.fillRect(card.x, card.y, card.width, card.height, false);
  renderer.drawRoundedRect(card.x, card.y, card.width, card.height, kFrameWidth, kCardRadius, true);

  const int innerX = card.x + kCardPadding + 4;
  const int innerW = card.width - 2 * (kCardPadding + 4);
  int y = card.y + kCardPadding;

  char percentText[8];
  snprintf(percentText, sizeof(percentText), "%u%%", static_cast<unsigned>(book.progressPercent));
  const int percentW = renderer.getTextWidth(SMALL_FONT_ID, percentText);
  const std::string title =
      renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), innerW - percentW - 12, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, innerX, y, title.c_str(), true, EpdFontFamily::BOLD);
  const int titleH = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, innerX + innerW - percentW,
                    y + titleH - renderer.getLineHeight(SMALL_FONT_ID), percentText, true);
  y += titleH + 2;

  if (!book.author.empty()) {
    const std::string author = renderer.truncatedText(SMALL_FONT_ID, book.author.c_str(), innerW);
    renderer.drawText(SMALL_FONT_ID, innerX, y, author.c_str(), true);
  }
  y += renderer.getLineHeight(SMALL_FONT_ID) + kBarGap;

  drawBar(renderer, innerX, y, innerW, book.progressPercent);
}

const uint8_t* iconFor(const UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Blocks:
      return BlocksIcon;
    default:
      return nullptr;
  }
}
// Worth laying out: the file is there and, for a BMP, its header reads. A JPG is only
// decoded when drawn; drawRecentBookCover drops it then if that fails.
bool imageUsable(const char* path) {
  if (path[0] == '\0' || !Storage.exists(path)) return false;
  if (FsHelpers::hasJpgExtension(std::string_view{path})) return true;
  HalFile file;
  if (!Storage.openFileForRead("HOME", path, file)) return false;
  Bitmap bitmap(file);
  return bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0;
}
}  // namespace

void BannerTheme::onHomeEnter() const {
  cardBookIndex = 0;
  topUsable = imageUsable(SETTINGS.homeTopImage);
  middleUsable = imageUsable(SETTINGS.homeMiddleImage);
}

void BannerTheme::forgetConvertedImages(const char* slot) {
  char slotPrefix[24];
  snprintf(slotPrefix, sizeof(slotPrefix), "%s%s_", kCachePrefix, slot);
  removeConvertedBanners([&](const char* file) {
    return isUnslottedBanner(file) || strncmp(file, slotPrefix, strlen(slotPrefix)) == 0;
  });
}

int BannerTheme::homeCoverThumbHeight(const GfxRenderer& renderer) const {
  const TileLayout l = computeLayout(renderer, tileRect(renderer), true, topUsable, middleUsable);
  // Thumbs fit a (0.6h, h) box; ask for one wide enough to fill the slot and crop the height.
  return std::max(l.coverHeight, l.slotWidth * 5 / 3 + 2);
}

bool BannerTheme::homeCoverBand(const GfxRenderer& renderer, const Rect rect, int& top, int& bottom) const {
  const TileLayout l = computeLayout(renderer, rect, true, topUsable, middleUsable);
  top = l.covers.y;
  bottom = l.covers.y + l.covers.height;
  return true;
}

void BannerTheme::drawRecentBookCover(GfxRenderer& renderer, const Rect rect,
                                      const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                      bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                      std::function<bool()> storeCoverBuffer) const {
  (void)bufferRestored;
  const bool hasBooks = !recentBooks.empty();
  TileLayout l = computeLayout(renderer, rect, hasBooks, topUsable, middleUsable);

  // Images and covers are drawn once; later frames restore the snapshot.
  if (!coverRendered) {
    // An image that fails to draw (say a JPG that will not decode) gives its space up: clear
    // the tile and lay it out again without it. Each pass drops at least one image, so this
    // runs at most three times.
    bool imagesDrawn = false;
    while (!imagesDrawn) {
      imagesDrawn = true;
      if (l.top.height > 0 && drawImageFitted(renderer, TOP_SLOT, SETTINGS.homeTopImage, l.top).width == 0) {
        topUsable = false;
        imagesDrawn = false;
      }
      if (l.middle.height > 0) {
        const Rect inner{l.middle.x + kImageInset, l.middle.y + kImageInset, l.middle.width - 2 * kImageInset,
                         l.middle.height - 2 * kImageInset};
        const Rect drawn = drawImageFitted(renderer, MIDDLE_SLOT, SETTINGS.homeMiddleImage, inner);
        if (drawn.width > 0) {
          renderer.maskRoundedRectOutsideCorners(drawn.x, drawn.y, drawn.width, drawn.height,
                                                 kCardRadius - kImageInset, Color::White);
          renderer.drawRoundedRect(l.middle.x, l.middle.y, l.middle.width, l.middle.height, kFrameWidth,
                                   kCardRadius, true);
        } else {
          middleUsable = false;
          imagesDrawn = false;
        }
      }
      if (!imagesDrawn) {
        renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
        l = computeLayout(renderer, rect, hasBooks, topUsable, middleUsable);
      }
    }
    drawCovers(renderer, l, recentBooks, homeCoverThumbHeight(renderer));
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  const int count = std::min(static_cast<int>(recentBooks.size()), kCoverCount);
  // The card shows the highlighted book, or the last one highlighted while the menu row has focus.
  if (selectorIndex >= 0 && selectorIndex < count) cardBookIndex = selectorIndex;
  if (hasBooks) drawBookCard(renderer, l.card, recentBooks[std::min(cardBookIndex, count - 1)]);
  if (selectorIndex >= 0 && selectorIndex < count) {
    const Rect slot = slotRect(l, selectorIndex);
    renderer.drawRoundedRect(slot.x - kSelectionInset, slot.y - kSelectionInset, slot.width + 2 * kSelectionInset,
                             slot.height + 2 * kSelectionInset, 3, 8, true);
  }
}

bool BannerTheme::homeMenuGrid(const GfxRenderer& renderer, const Rect rect, const int buttonCount,
                               HomeMenuGrid& grid) const {
  const int width = rect.width - 2 * kSidePadding;
  const int columns = std::max(1, buttonCount);
  grid.columns = columns;
  grid.left = rect.x + kSidePadding;
  grid.top = std::min(rect.y, menuTop(renderer));
  grid.tileWidth = width / columns;
  grid.tileHeight = kMenuHeight;
  grid.colStep = grid.tileWidth;
  grid.rowStep = kMenuHeight;
  return true;
}

void BannerTheme::drawButtonMenu(GfxRenderer& renderer, const Rect rect, const int buttonCount,
                                 const int selectedIndex, const std::function<std::string(int index)>& buttonLabel,
                                 const std::function<UIIcon(int index)>& rowIcon) const {
  (void)buttonLabel;
  HomeMenuGrid grid;
  homeMenuGrid(renderer, rect, buttonCount, grid);

  const int pillWidth = grid.tileWidth * grid.columns;
  renderer.fillRoundedRect(grid.left, grid.top, pillWidth, kMenuHeight, kMenuHeight / 2, Color::White);
  renderer.drawRoundedRect(grid.left, grid.top, pillWidth, kMenuHeight, kFrameWidth, kMenuHeight / 2, true);

  for (int i = 0; i < buttonCount; i++) {
    const int cx = grid.left + i * grid.colStep + grid.tileWidth / 2;
    const int cy = grid.top + kMenuHeight / 2;
    const bool selected = selectedIndex == i;
    if (selected) {
      renderer.fillRoundedRect(cx - kIconHighlight / 2, cy - kIconHighlight / 2, kIconHighlight, kIconHighlight, 12,
                               Color::Black);
    }
    if (rowIcon != nullptr) {
      if (const uint8_t* icon = iconFor(rowIcon(i))) {
        renderer.drawIcon(icon, cx - kIconSize / 2, cy - kIconSize / 2, kIconSize, !selected);
      }
    }
  }
}
