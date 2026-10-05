// The story-only stages on the Versus / Training stage select: Namek as it
// explodes (NMH_A, stage 6), the wasteland under a purple sky (ETH_A, 12) and
// the cliffs by the sea under a stormy sky (ETI_A, 22). The battles of Z
// Chronicles load them like any other stage, but the stage select shows only
// the 5 locations - each with 2 stages swapped with Y - and RANDOM. They're
// added as 3 more entries before RANDOM.
//
// The stage select (sub_82263628 sets it up, sub_82263920 runs it) keeps an
// object of 344 bytes (address at [0x8424D7A0]) with room for 16 entries:
//   +24  stage of each entry           +88  its partner (Y)
//   +152 entry count, +156 the ones the cursor goes through
//   +160 locked, +176 partner locked   +192 / +208 NEW
//   +276 RANDOM's pool, its count at +340
// Each entry's card on the carousel and the big picture are sprites of the
// screen's AMA (the stage's group picks them: tables at 0x826E0760 and
// 0x826E0778); the new entries use sprite 449, a 540x236 thumbnail sprite the
// game doesn't use, pointed at textures 33-35 of UISTS_TOP.NUT, which no
// sprite uses either; their pictures are put in those textures as the file
// loads. The game has no names for them; theirs are below.
// Online the list stays as it is (the other side may not have the stages).

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/memory/utils.h>
#include <rex/net/session.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>

// The new entries' pictures (made from screenshots of the stages):
// kStageThumbs[i] for NMH_A, ETH_A, ETI_A - 540x236 DXT5 as the NUT keeps it,
// kStageThumbSize bytes, nullptr for none (the texture keeps the game's).
#if __has_include("burstlimit_stage_thumbs.inc")
#include "burstlimit_stage_thumbs.inc"
#else
#pragma message("burstlimit_stage_thumbs.inc not found: the extra stage-select entries show placeholder pictures")
static constexpr uint32_t kStageThumbSize = 127440;
static const uint8_t* const kStageThumbs[3] = {nullptr, nullptr, nullptr};
#endif

REXCVAR_DEFINE_BOOL(extra_stages, true, "Patches",
                    "The story-only stages on the Versus / Training stage select (offline): Namek "
                    "exploding, the purple-sky wasteland and the seaside cliffs");

namespace {

constexpr int32_t kExtraStages[3] = {6, 12, 22};  // NMH_A, ETH_A, ETI_A
constexpr int32_t kStageRandom = 26;
// The 6 entries the game sets up: 5 locations and RANDOM.
constexpr int32_t kGameList[6] = {8, 24, 10, 14, 4, kStageRandom};
// The unused 540x236 thumbnail sprite and the unused textures it can show.
constexpr uint16_t kThumbnailSprite = 449;
constexpr uint16_t kThumbnailSpriteTexture = 32;
constexpr uint16_t kThumbnailTextures[3] = {33, 34, 35};
constexpr const char16_t* kNames[3] = {u"Dying Namek", u"Wasteland", u"Seaside Cliffs"};

constexpr uint32_t kObjectCount = 152;
constexpr uint32_t kObjectNavigable = 156;
constexpr uint32_t kObjectStages = 24;
constexpr uint32_t kObjectLocked = 160;
constexpr uint32_t kObjectPartnerLocked = 176;
constexpr uint32_t kObjectNew = 192;
constexpr uint32_t kObjectPartnerNew = 208;
constexpr uint32_t kObjectPool = 276;
constexpr uint32_t kObjectPoolCount = 340;
constexpr uint32_t kObjectEntries = 16;

// The stage select's globals (G): +12 the object above, +20 the carousel's 16
// cards (8 bytes each, the sprite first; card i is entry i), +24 the block of
// the big picture (its sprite at +32, that sprite's part at +8).
constexpr uint32_t kSelectGlobals = 0x8424D794;
constexpr uint32_t kGlobalsObject = 12;
constexpr uint32_t kGlobalsCards = 20;
constexpr uint32_t kGlobalsPreview = 24;
constexpr uint32_t kCardStride = 8;
constexpr uint32_t kPreviewPart = 8;
constexpr uint32_t kPreviewSprite = 32;

// Textures 33-35 of UISTS_TOP.NUT. The file is NTXR version 1: each texture is
// its header (0x50 bytes), then its data. This is their header (33's); the 3
// differ only in the low byte of the GIDX (+0x4B): 0xA1, 0xA2, 0xA3.
constexpr uint32_t kThumbHeaderSize = 0x50;
constexpr uint32_t kThumbDataSize = 0x1F1D0;  // 540x236 DXT5, 1 level
constexpr uint32_t kThumbGidxByte = 0x4B;
constexpr uint8_t kThumbHeader[kThumbHeaderSize] = {
    0x00, 0x01, 0xF2, 0x20, 0x00, 0x00, 0x00, 0x00,  // +0x00 header + data size
    0x00, 0x01, 0xF1, 0xD0, 0x00, 0x50, 0x00, 0x00,  // +0x08 data size, header size
    0x00, 0x01, 0x00, 0x02, 0x02, 0x1C, 0x00, 0xEC,  // +0x10 1 level, DXT5, 540 x 236
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // +0x18
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // +0x20
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // +0x28
    0x65, 0x58, 0x74, 0x00, 0x00, 0x00, 0x00, 0x20,  // +0x30 "eXt"
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,  // +0x38 1 tag
    0x47, 0x49, 0x44, 0x58, 0x00, 0x00, 0x00, 0x10,  // +0x40 "GIDX"
    0x00, 0x00, 0x89, 0xA1, 0x00, 0x00, 0x00, 0x00,  // +0x48 0x89A1 (+ the entry)
};
static_assert(kStageThumbSize == kThumbDataSize);
// FNV-1a 64 of the game's data of textures 33, 34, 35 (the US UISTS_TOP.NUT):
// another version of the file is left alone.
constexpr uint64_t kThumbOriginal[3] = {0xB37331AFD82152BBull, 0x4C7B5EB936536F28ull,
                                        0x60C1C08B7732F5F4ull};
constexpr const char* kThumbStages[3] = {"NMH_A", "ETH_A", "ETI_A"};
std::atomic<bool> g_thumb_mismatch_logged{false};

// The stage select set up with the new entries (offline, extra_stages on).
bool g_active = false;
uint32_t g_names[3] = {};
// The big picture last made for a new entry (0 = it isn't one), and which.
uint32_t g_preview_sprite = 0;
int g_preview_extra = -1;

rex::memory::Memory* GuestMemory() {
  auto* runtime = rex::Runtime::instance();
  return runtime ? runtime->memory() : nullptr;
}

uint32_t R32(rex::memory::Memory* memory, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual<uint8_t*>(address));
}
uint16_t R16(rex::memory::Memory* memory, uint32_t address) {
  return rex::memory::load_and_swap<uint16_t>(memory->TranslateVirtual<uint8_t*>(address));
}
uint8_t R8(rex::memory::Memory* memory, uint32_t address) {
  return *memory->TranslateVirtual<uint8_t*>(address);
}
void W32(rex::memory::Memory* memory, uint32_t address, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(memory->TranslateVirtual<uint8_t*>(address), value);
}
void W16(rex::memory::Memory* memory, uint32_t address, uint16_t value) {
  rex::memory::store_and_swap<uint16_t>(memory->TranslateVirtual<uint8_t*>(address), value);
}
void W8(rex::memory::Memory* memory, uint32_t address, uint8_t value) {
  *memory->TranslateVirtual<uint8_t*>(address) = value;
}

uint64_t Fnv1a64(const uint8_t* data, size_t size) {
  uint64_t hash = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 0x100000001B3ull;
  }
  return hash;
}

int ExtraIndex(int32_t stage) {
  for (int i = 0; i < 3; ++i) {
    if (kExtraStages[i] == stage) {
      return i;
    }
  }
  return -1;
}

// The names, as the game's text (UTF-16 big-endian) in guest memory - made
// once and kept.
void EnsureNames(rex::memory::Memory* memory) {
  for (int i = 0; i < 3; ++i) {
    if (g_names[i]) {
      continue;
    }
    const char16_t* name = kNames[i];
    uint32_t length = 0;
    while (name[length]) {
      ++length;
    }
    const uint32_t address = memory->SystemHeapAlloc((length + 1) * 2);
    if (!address) {
      continue;
    }
    for (uint32_t c = 0; c <= length; ++c) {
      W16(memory, address + c * 2, uint16_t(name[c]));
    }
    g_names[i] = address;
  }
}

// A card or the big picture of a new entry: the unused thumbnail sprite shows
// the entry's own texture. The sprite's data (+48) keeps its texture index at
// +20, which the game copies to the carousel every frame - but its animation
// also sets it again every frame (sub_82139598 -> sub_82138C68 ->
// sub_82136360), so this is done after that each time.
void ShowThumbnail(rex::memory::Memory* memory, uint32_t sprite, int extra) {
  const uint32_t data = R32(memory, sprite + 48);
  if (data && R16(memory, data + 20) == kThumbnailSpriteTexture) {
    W16(memory, data + 20, kThumbnailTextures[extra]);
  }
}

}  // namespace

// Mid-asm hook at 0x822637FC in sub_82263628 (stage select setup), before it
// fills partners, locks and RANDOM's pool from the list (bl sub_82261DF8):
// r31 = the screen's object, with the game's 6 entries.
void BurstLimitStagesInit(PPCRegister& r31) {
  g_active = false;
  g_preview_sprite = 0;
  g_preview_extra = -1;
  auto* memory = GuestMemory();
  const uint32_t object = r31.u32;
  if (!memory || !object || !REXCVAR_GET(extra_stages) || rex::net::IsGameSessionOpen()) {
    return;
  }
  if (R32(memory, object + kObjectCount) != 6 || R32(memory, object + kObjectNavigable) != 6) {
    return;
  }
  for (uint32_t i = 0; i < 6; ++i) {
    if (int32_t(R32(memory, object + kObjectStages + i * 4)) != kGameList[i]) {
      return;
    }
  }
  // The 3 stages, then RANDOM (it has to stay last).
  const int32_t entries[4] = {kExtraStages[0], kExtraStages[1], kExtraStages[2], kStageRandom};
  for (uint32_t i = 0; i < 4; ++i) {
    W32(memory, object + kObjectStages + (5 + i) * 4, uint32_t(entries[i]));
    W8(memory, object + kObjectLocked + 5 + i, 0);
    W8(memory, object + kObjectNew + 5 + i, 0);
  }
  W32(memory, object + kObjectCount, 9);
  W32(memory, object + kObjectNavigable, 9);
  EnsureNames(memory);
  g_active = true;
  REXLOG_INFO("Stage select: the 3 story stages added (9 entries)");
}

// Mid-asm hook at 0x82233EAC, the end of sub_82233E78 (is stage r4 unlocked,
// r3 = the answer): the new entries are always unlocked (the save isn't
// touched).
void BurstLimitStagesUnlock(PPCRegister& r3, PPCRegister& r4) {
  if (g_active && ExtraIndex(r4.s32) >= 0) {
    r3.u64 = 1;
  }
}

// Mid-asm hook at 0x82263800 in sub_82263628, after sub_82261DF8 filled the
// partners and RANDOM's pool: r18 = the stage the cursor goes back to, r31 =
// the screen's object.
void BurstLimitStagesRefresh(PPCRegister& r18, PPCRegister& r31) {
  if (!g_active) {
    // Not added (online, or turned off): don't go back to a missing stage.
    if (ExtraIndex(r18.s32) >= 0) {
      r18.u64 = 8;
    }
    return;
  }
  auto* memory = GuestMemory();
  const uint32_t object = r31.u32;
  if (!memory || !object) {
    return;
  }
  // No Y partner (each stage is its own partner): no "Change Color".
  const uint32_t count = std::min<uint32_t>(R32(memory, object + kObjectCount), kObjectEntries);
  for (uint32_t i = 0; i < count; ++i) {
    if (ExtraIndex(int32_t(R32(memory, object + kObjectStages + i * 4))) >= 0) {
      W8(memory, object + kObjectPartnerLocked + i, 1);
      W8(memory, object + kObjectPartnerNew + i, 0);
    }
  }
  // The pool has every stage and its partner: the new ones twice.
  const uint32_t pool_count =
      std::min<uint32_t>(R32(memory, object + kObjectPoolCount), kObjectEntries);
  uint32_t kept = 0;
  for (uint32_t i = 0; i < pool_count; ++i) {
    const uint32_t stage = R32(memory, object + kObjectPool + i * 4);
    bool seen = false;
    for (uint32_t j = 0; j < kept; ++j) {
      if (R32(memory, object + kObjectPool + j * 4) == stage) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      W32(memory, object + kObjectPool + kept * 4, stage);
      ++kept;
    }
  }
  W32(memory, object + kObjectPoolCount, kept);
}

// Mid-asm hook after 0x82260680 in sub_822605A0 (carousel cards): r11 = the
// card's sprite from the stage's group, r27 -> the entry's stage.
void BurstLimitStagesCardPart(PPCRegister& r11, PPCRegister& r27) {
  auto* memory = GuestMemory();
  if (g_active && memory && ExtraIndex(int32_t(R32(memory, r27.u32))) >= 0) {
    r11.u64 = kThumbnailSprite;
  }
}

// Mid-asm hook at 0x822606AC in sub_822605A0, after the card's sprite is made:
// r3 = the sprite, r27 -> the entry's stage, r29 = the entry, r30 = the
// screen's object.
void BurstLimitStagesCardTexture(PPCRegister& r3, PPCRegister& r27, PPCRegister& r29,
                                 PPCRegister& r30) {
  auto* memory = GuestMemory();
  if (!g_active || !memory || !r3.u32) {
    return;
  }
  const int extra = ExtraIndex(int32_t(R32(memory, r27.u32)));
  if (extra >= 0 && !R8(memory, r30.u32 + kObjectLocked + r29.u32)) {
    ShowThumbnail(memory, r3.u32, extra);
  }
}

// Mid-asm hook after 0x82260920 in sub_82260710 (the big picture of the stage
// under the cursor): r10 = its sprite from the stage's group, r25 = the stage.
void BurstLimitStagesPreviewPart(PPCRegister& r10, PPCRegister& r25) {
  if (g_active && ExtraIndex(r25.s32) >= 0) {
    r10.u64 = kThumbnailSprite;
  }
}

// Mid-asm hook at 0x82260C20 in sub_82260710, after the big picture's sprite
// is made: r3 = the sprite, r25 = the stage, r31 = the picture's block (its
// sprite at +8).
void BurstLimitStagesPreviewTexture(PPCRegister& r3, PPCRegister& r25, PPCRegister& r31) {
  auto* memory = GuestMemory();
  if (!g_active || !memory || !r3.u32) {
    return;
  }
  const int extra = ExtraIndex(r25.s32);
  if (extra >= 0 && R16(memory, r31.u32 + kPreviewPart) == kThumbnailSprite) {
    g_preview_sprite = r3.u32;
    g_preview_extra = extra;
    ShowThumbnail(memory, r3.u32, extra);
  } else {
    g_preview_sprite = 0;
    g_preview_extra = -1;
  }
}

// Mid-asm hook at 0x822620E4 in sub_82261FC8 (the stage select's frame), after
// it has played the animations of the cards and of the big picture, which set
// their texture back to the sprite's own, and before it copies them to the
// carousel: r22 = the stage select's globals.
void BurstLimitStagesPictures(PPCRegister& r22) {
  auto* memory = GuestMemory();
  if (!g_active || !memory || r22.u32 != kSelectGlobals) {
    return;
  }
  const uint32_t object = R32(memory, kSelectGlobals + kGlobalsObject);
  const uint32_t cards = R32(memory, kSelectGlobals + kGlobalsCards);
  if (object && cards) {
    const uint32_t count = std::min<uint32_t>(R32(memory, object + kObjectCount), kObjectEntries);
    for (uint32_t i = 0; i < count; ++i) {
      const int extra = ExtraIndex(int32_t(R32(memory, object + kObjectStages + i * 4)));
      const uint32_t sprite = R32(memory, cards + i * kCardStride);
      if (extra >= 0 && sprite && !R8(memory, object + kObjectLocked + i)) {
        ShowThumbnail(memory, sprite, extra);
      }
    }
  }
  const uint32_t preview = R32(memory, kSelectGlobals + kGlobalsPreview);
  if (preview && g_preview_sprite && g_preview_extra >= 0 &&
      R32(memory, preview + kPreviewSprite) == g_preview_sprite &&
      R16(memory, preview + kPreviewPart) == kThumbnailSprite) {
    ShowThumbnail(memory, g_preview_sprite, g_preview_extra);
  }
}

// Mid-asm hook at 0x82260C64 in sub_82260710 (stw r3,0(r24): the name's text):
// r3 = the text, r25 = the stage.
void BurstLimitStagesName(PPCRegister& r3, PPCRegister& r25) {
  const int extra = g_active ? ExtraIndex(r25.s32) : -1;
  if (extra >= 0 && g_names[extra]) {
    r3.u64 = g_names[extra];
  }
}

// Mid-asm hook at the start of sub_82263538 (the stage's preview movie, r3 =
// the stage): none for the new stages, like RANDOM (the game would play a
// Namek clip).
bool BurstLimitStagesMovie(PPCRegister& r3) {
  return g_active && ExtraIndex(r3.s32) >= 0;
}

// Mid-asm hook at 0x820EB784 in sub_820EB688 (makes the textures of an NTXR
// file in memory - every one the game loads), before bl sub_820F3F88 makes one
// texture: r26 = the file's NTXR version, r29 = the texture's index in the
// file, r30 = its header. sub_820F3F88 copies the data into the texture's own
// memory right away (sub_820F89F8 -> sub_82512BC0, tiled), so what's written
// here is what the texture shows. A texture still there from an earlier load
// isn't made again (sub_820F37F8 finds it by handle), so this runs on every
// load, and whatever extra_stages is: the game never shows these textures.
void BurstLimitStagesThumbnailData(PPCRegister& r26, PPCRegister& r29, PPCRegister& r30) {
  if (r26.u32 != 1 || !r30.u32) {
    return;
  }
  auto* memory = GuestMemory();
  if (!memory) {
    return;
  }
  uint8_t* header = memory->TranslateVirtual<uint8_t*>(r30.u32);
  if (std::memcmp(header, kThumbHeader, kThumbGidxByte) != 0 ||
      std::memcmp(header + kThumbGidxByte + 1, kThumbHeader + kThumbGidxByte + 1,
                  kThumbHeaderSize - kThumbGidxByte - 1) != 0) {
    return;
  }
  const int extra = int(header[kThumbGidxByte]) - int(kThumbHeader[kThumbGidxByte]);
  if (extra < 0 || extra >= 3 || !kStageThumbs[extra]) {
    return;
  }
  const uint8_t* thumb = kStageThumbs[extra];
  uint8_t* data = header + kThumbHeaderSize;
  if (Fnv1a64(data, kThumbDataSize) != kThumbOriginal[extra]) {
    // Ours already (the same file in memory made again), or not the game's
    // picture (another version of the file): left as it is.
    if (std::memcmp(data, thumb, kThumbDataSize) != 0 && !g_thumb_mismatch_logged.exchange(true)) {
      REXLOG_WARN("Stage select: UISTS_TOP texture {} isn't the expected picture; the {} "
                  "thumbnail isn't put in",
                  r29.u32, kThumbStages[extra]);
    }
    return;
  }
  std::memcpy(data, thumb, kThumbDataSize);
  REXLOG_INFO("Stage select: the {} thumbnail put in UISTS_TOP texture {}", kThumbStages[extra],
              r29.u32);
}
