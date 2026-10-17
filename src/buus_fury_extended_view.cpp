#include "buus_fury_extended_view.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <list>
#include <unordered_map>
#include <utility>

#include "gba_bus.h"
#include "gba_ppu.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"

extern "C" unsigned g_ws_active;
extern "C" unsigned g_ws_extra_left;
extern "C" unsigned g_ws_extra_right;

namespace buus_fury {
namespace {

// Buu's Fury keeps a four-entry cache of decompressed 32x32 chunks for each
// authored overworld layer and streams their visible rings into VRAM screen
// blocks 28..31. A 480-pixel view can cross three chunk columns, so a margin
// occasionally needs one chunk beyond those four native caches. The resource
// layout and variable-bit LZ decoder below mirror the renderer at 0x08007C12
// and the ARM routine installed at 0x03000040.
constexpr std::uint32_t kFieldLayerVtable = 0x08049410u;
constexpr std::uint32_t kCameraLeft = 0x030019BCu;
constexpr std::uint32_t kCameraRight = 0x030019C4u;
constexpr std::uint32_t kCameraX = 0x030019CCu;
constexpr std::uint32_t kCameraY = 0x030019D0u;
constexpr std::uint32_t kActorFixedX = 0x1A0u;
constexpr std::uint32_t kActorFixedY = 0x1A4u;
// Global that holds the object pointer of the currently controlled hero.
// Verified by TCP probes in two different areas (King Kai's field and a late
// map): the pointer resolves to the only object of the hero class
// (vtable 0x0804C9F0) and tracks the actor the camera follows.
constexpr std::uint32_t kControlledActor = 0x03001B94u;
// The hero's native walking step is 0x160 fixed-point units per frame
// (1.375 px measured over 14 frames). Movement turbo scales an ordinary step
// by 1.5x; anything larger is a teleport/respawn/area load and is ignored.
constexpr std::int32_t kWalkStepLimit = 0x300;
constexpr std::size_t kFieldChunkBytes = 0x800u;
constexpr int kFieldCacheBiasX = 8;
constexpr int kFieldCacheBiasY = 48;
// How far past the native camera rect the actor renderer is allowed to reach.
//
// This is deliberately NOT the full margin: it is only enough for a composite
// actor that straddles the native edge to stay whole. Most field stages are
// authored for 240px, so past that there is no background and revealing an
// actor out there leaves it standing on empty space. Keeping the reach small
// means an actor only appears in a margin when it is genuinely on its way in.
constexpr int kActorCullPadding = 32;
std::uint32_t g_field_objects[4] = {};
bool g_turbo_active = false;
std::uint32_t g_turbo_player = 0u;
// Decoded-chunk cache with a most-recently-used bound: a long session streams
// thousands of distinct descriptors (they live in ROM, so entries can never
// go stale), and this keeps memory flat. Evicting one only costs a re-decode.
constexpr std::size_t kDecodedChunkCacheCap = 384;
struct DecodedChunkCache {
    struct Entry {
        std::array<std::uint8_t, kFieldChunkBytes> pixels{};
        std::list<std::uint32_t>::iterator lru{};
    };
    std::unordered_map<std::uint32_t, Entry> map;
    std::list<std::uint32_t> order;  // front = most recently used
    void clear() {
        map.clear();
        order.clear();
    }
};
DecodedChunkCache g_decoded_chunks;

// Per-frame margin tile-entry memo. The tilemap provider is invoked once per
// margin pixel (~50K per composed frame at 480 pixels), and every 8x8 screen
// tile resolves to the same authored 8x8 field tile, so field_entry() would
// recompute an identical result dozens of times per frame. The bus state it
// reads (layer scroll, chunk-slot owners, decoded chunks) is stable within a
// composed frame, so a direct-mapped memo keyed by (layer object, world tile)
// and tagged with the frame generation is exact; a collision merely re-resolves.
constexpr std::size_t kFieldTileCacheSize = 2048u;  // power of two
struct FieldTileCacheSlot {
    std::uint32_t generation = 0u;
    std::uint32_t object = 0u;
    std::int32_t tile_x = 0;
    std::int32_t tile_y = 0;
    std::uint16_t entry = 0u;
    bool valid = false;
};
FieldTileCacheSlot g_field_tile_cache[kFieldTileCacheSize];

bool g_single_chunk_bg3_scene = false;
// The overworld layer cache is rebuilt when the game re-creates field objects
// (new area / new game). Scanning ALL of EWRAM to re-find them is expensive, so
// the scan is gated to at most once per composed frame. Without the gate, a
// scene whose BG/IO state matches is_overworld before the layer objects exist
// (the first frames of a new game) would trigger one full 64K-iteration scan
// per margin pixel the tilemap provider visits — a multi-second freeze per
// frame.
std::uint32_t g_resolve_frame = 0;
// Frame zero is a real composition (not an uninitialized marker). Start with
// a distinct sentinel so the field objects are resolved on the first BG read.
std::uint32_t g_last_scan_frame = std::numeric_limits<std::uint32_t>::max();

struct ObjXHistory {
    std::uint16_t attr0_shape = 0;
    std::uint16_t attr1_shape = 0;
    std::uint16_t attr2 = 0;
    int raw_y = 0;
    int resolved_x = 0;
    bool valid = false;
};

struct ActorHistory {
    std::uint32_t object = 0;
    std::uint32_t generation = 0;
};

std::array<ObjXHistory, 128> g_previous_obj_x{};
std::array<ObjXHistory, 128> g_current_obj_x{};
std::array<ActorHistory, 128> g_actor_history{};
std::size_t g_actor_history_cursor = 0;
std::uint32_t g_obj_x_generation = 1;

gba::GbaBus* active_bus() {
    return gbarecomp::active_bus();
}

std::uint32_t ewram32(gba::GbaBus* bus, std::uint32_t address) {
    if (!bus || address < 0x02000000u || address + 3u >= 0x02040000u)
        return 0;
    const std::uint8_t* p = bus->ewram_ptr() + address - 0x02000000u;
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint8_t mem8(gba::GbaBus* bus, std::uint32_t address) {
    if (!bus) return 0;
    if (address >= 0x02000000u && address < 0x02040000u)
        return bus->ewram_ptr()[address - 0x02000000u];
    if (address >= 0x03000000u && address < 0x03008000u)
        return bus->iwram_ptr()[address - 0x03000000u];
    if (address >= 0x08000000u &&
        address - 0x08000000u < bus->rom_size()) {
        return bus->rom_ptr()[address - 0x08000000u];
    }
    return 0;
}

std::uint16_t mem16(gba::GbaBus* bus, std::uint32_t address) {
    return static_cast<std::uint16_t>(mem8(bus, address)) |
        static_cast<std::uint16_t>(mem8(bus, address + 1u) << 8);
}

std::uint32_t mem32(gba::GbaBus* bus, std::uint32_t address) {
    return static_cast<std::uint32_t>(mem16(bus, address)) |
        (static_cast<std::uint32_t>(mem16(bus, address + 2u)) << 16);
}

int distance(int a, int b) {
    return std::abs(a - b);
}

bool adjacent_obj_generation(std::uint32_t generation) {
    return generation == g_obj_x_generation ||
        generation + 1u == g_obj_x_generation;
}

void remember_actor(gba::GbaBus* bus, std::uint32_t object) {
    if (object < 0x02000000u ||
        object + kActorFixedY + 3u >= 0x02040000u) {
        return;
    }
    const std::uint32_t vtable = mem32(bus, object);
    if (vtable < 0x08000000u || vtable >= 0x0A000000u)
        return;

    for (ActorHistory& actor : g_actor_history) {
        if (actor.object == object) {
            actor.generation = g_obj_x_generation;
            return;
        }
    }
    ActorHistory& actor = g_actor_history[
        g_actor_history_cursor++ % g_actor_history.size()];
    actor.object = object;
    actor.generation = g_obj_x_generation;
}

bool match_actor_x(gba::GbaBus* bus, int raw_x, int raw_y, int* out_x) {
    if (!bus || !out_x) return false;

    const int camera_x =
        static_cast<std::int32_t>(mem32(bus, kCameraX));
    const int camera_y =
        static_cast<std::int32_t>(mem32(bus, kCameraY));
    int best_score = std::numeric_limits<int>::max();
    int best_x = raw_x;
    for (const ActorHistory& actor : g_actor_history) {
        if (!actor.object || !adjacent_obj_generation(actor.generation))
            continue;
        const std::uint32_t vtable = mem32(bus, actor.object);
        if (vtable < 0x08000000u || vtable >= 0x0A000000u)
            continue;

        const int actor_x = static_cast<std::int32_t>(
            mem32(bus, actor.object + kActorFixedX)) >> 8;
        const int actor_y = static_cast<std::int32_t>(
            mem32(bus, actor.object + kActorFixedY)) >> 8;
        const int expected_x = actor_x - camera_x;
        const int expected_y = actor_y - camera_y;
        const int dy = distance(raw_y, expected_y);
        if (dy > 96) continue;

        const int candidates[] = {raw_x, raw_x - 512};
        for (const int candidate : candidates) {
            const int dx = distance(candidate, expected_x);
            if (dx > 96) continue;
            const int score = dx + dy;
            if (score < best_score) {
                best_score = score;
                best_x = candidate;
            }
        }
    }
    if (best_score == std::numeric_limits<int>::max())
        return false;
    *out_x = best_x;
    return true;
}

class FieldBitReader {
public:
    FieldBitReader(gba::GbaBus* bus, std::uint32_t address)
        : bus_(bus), address_(address) {}

    std::uint32_t bits(unsigned count) {
        if (count > 31u) {
            ok_ = false;
            return 0u;
        }
        std::uint32_t value = 0u;
        while (count--)
            value = (value << 1) | bit();
        return value;
    }

    bool ok() const { return ok_; }

private:
    std::uint32_t bit() {
        if (remaining_ == 0u) {
            if (!bus_ || address_ < 0x08000000u ||
                address_ - 0x08000000u + 4u > bus_->rom_size()) {
                ok_ = false;
                return 0u;
            }
            word_ = mem32(bus_, address_);
            address_ += 4u;
            remaining_ = 32u;
        }
        const std::uint32_t value = word_ >> 31;
        word_ <<= 1;
        --remaining_;
        return value;
    }

    gba::GbaBus* bus_ = nullptr;
    std::uint32_t address_ = 0u;
    std::uint32_t word_ = 0u;
    unsigned remaining_ = 0u;
    bool ok_ = true;
};

bool decode_field_chunk(
    gba::GbaBus* bus, std::uint32_t descriptor,
    std::array<std::uint8_t, kFieldChunkBytes>* out) {
    if (!bus || !out || descriptor < 0x08000000u ||
        descriptor - 0x08000000u + 8u > bus->rom_size())
        return false;

    const std::uint32_t kind = mem32(bus, descriptor);
    const std::uint32_t output_size = mem32(bus, descriptor + 4u);
    if (output_size != kFieldChunkBytes)
        return false;

    if (kind == 0u) {
        if (descriptor - 0x08000000u + 8u + output_size >
            bus->rom_size())
            return false;
        for (std::size_t i = 0; i < out->size(); ++i)
            (*out)[i] = mem8(bus, descriptor + 8u +
                static_cast<std::uint32_t>(i));
        return true;
    }
    if (kind != 1u && kind != 2u)
        return false;

    FieldBitReader reader(bus, descriptor + 8u);
    std::size_t produced = 0u;
    unsigned distance_bits = 8u;
    unsigned literal_bits = kind;
    std::uint32_t literal_base = 0u;
    std::uint32_t last_distance = 1u;

    auto append = [&](std::uint32_t value) {
        if (produced >= out->size()) return false;
        (*out)[produced++] = static_cast<std::uint8_t>(value);
        return true;
    };
    auto copy = [&](std::uint32_t distance, std::uint32_t length) {
        if (distance == 0u || distance > produced ||
            length > out->size() - produced)
            return false;
        while (length--) {
            (*out)[produced] = (*out)[produced - distance];
            ++produced;
        }
        return true;
    };
    auto gamma = [&]() {
        std::uint32_t value = 1u;
        do {
            value = (value << 1) | reader.bits(1u);
        } while (reader.bits(1u) != 0u && reader.ok());
        return value;
    };

    bool terminated = false;
    for (unsigned commands = 0; commands < 65536u && !terminated;
         ++commands) {
        if (reader.bits(1u) != 0u) {
            if (literal_bits > 24u ||
                !append(reader.bits(literal_bits) + literal_base))
                return false;
            continue;
        }

        if (reader.bits(1u) != 0u) {
            const std::uint32_t code = gamma();
            std::uint32_t distance = 0u;
            std::uint32_t length = 0u;
            if (code == 2u) {
                distance = last_distance;
                length = gamma();
            } else {
                if (distance_bits > 24u || code < 3u)
                    return false;
                distance =
                    reader.bits(distance_bits) +
                    ((code - 3u) << distance_bits);
                last_distance = distance;
                length = gamma();
                if (distance >= 0x10000u)
                    length += 3u;
                else if (distance >= 0x37FFu)
                    length += 2u;
                else if (distance >= 0x027Fu)
                    length += 1u;
                else if (distance <= 0x007Fu)
                    length += 4u;
            }
            if (!copy(distance, length))
                return false;
            continue;
        }

        if (reader.bits(1u) == 0u) {
            const std::uint32_t distance = reader.bits(7u);
            if (distance != 0u) {
                last_distance = distance;
                if (!copy(distance, reader.bits(2u) + 2u))
                    return false;
            } else {
                const unsigned code = reader.bits(2u);
                if (code == 0u) {
                    terminated = true;
                } else {
                    distance_bits = reader.bits(code + 3u);
                }
            }
            continue;
        }

        const int short_code =
            static_cast<int>(reader.bits(4u)) - 1;
        if (short_code == 0) {
            if (!append(0u)) return false;
        } else if (short_code > 0) {
            if (!copy(static_cast<unsigned>(short_code), 1u))
                return false;
        } else if (reader.bits(1u) != 0u) {
            do {
                for (unsigned i = 0; i < 256u; ++i) {
                    if (!append(reader.bits(8u))) return false;
                }
            } while (reader.bits(1u) != 0u && reader.ok());
        } else {
            literal_base = 0u;
            literal_bits = 7u + reader.bits(1u);
            if (literal_bits == 7u)
                literal_base = reader.bits(8u);
        }
        if (!reader.ok())
            return false;
    }
    return reader.ok() && terminated && produced == out->size();
}

const std::uint8_t* decoded_field_chunk(
    gba::GbaBus* bus, std::uint32_t descriptor) {
    DecodedChunkCache& cache = g_decoded_chunks;
    const auto found = cache.map.find(descriptor);
    if (found != cache.map.end()) {
        cache.order.splice(cache.order.begin(), cache.order,
                           found->second.lru);
        return found->second.pixels.data();
    }

    DecodedChunkCache::Entry entry;
    if (!decode_field_chunk(bus, descriptor, &entry.pixels))
        return nullptr;
    cache.order.push_front(descriptor);
    entry.lru = cache.order.begin();
    const auto inserted = cache.map.emplace(descriptor, std::move(entry));
    if (cache.map.size() > kDecodedChunkCacheCap) {
        const std::uint32_t victim = cache.order.back();
        cache.order.pop_back();
        cache.map.erase(victim);
    }
    return inserted.first->second.pixels.data();
}

bool field_object_valid(
    gba::GbaBus* bus, std::uint32_t object, int layer) {
    if (!bus || object < 0x02000000u || object + 0x4Bu >= 0x02040000u)
        return false;
    if (ewram32(bus, object) != kFieldLayerVtable ||
        (ewram32(bus, object + 4u) & 0xFFu) !=
            static_cast<unsigned>(layer)) {
        return false;
    }
    const std::uint32_t resource = ewram32(bus, object + 8u);
    // +0x3C/+0x40 are 10-bit fixed-point parallax scales copied from
    // resource+0x04/+0x08 (1024 = 1x), not field dimensions.
    const std::uint32_t scale_x = ewram32(bus, object + 0x3Cu);
    const std::uint32_t scale_y = ewram32(bus, object + 0x40u);
    return resource >= 0x08000000u &&
        resource - 0x08000000u + 0x18u < bus->rom_size() &&
        scale_x > 0u && scale_x <= 4096u &&
        scale_y > 0u && scale_y <= 4096u;
}

void resolve_field_objects(gba::GbaBus* bus) {
    bool current = true;
    for (int layer = 0; layer < 4; ++layer)
        current &= field_object_valid(bus, g_field_objects[layer], layer);
    if (current) {
        g_last_scan_frame = g_resolve_frame;
        return;
    }
    // Scan at most once per composed frame. While the game is mid-transition
    // (DISPCNT already field-shaped but layer objects not yet created) the
    // tilemap provider visits ~50K margin pixels per frame; each must NOT
    // trigger a fresh 64K-iteration EWRAM sweep.
    if (g_last_scan_frame == g_resolve_frame)
        return;
    g_last_scan_frame = g_resolve_frame;

    for (std::uint32_t& object : g_field_objects)
        object = 0u;
    for (std::uint32_t offset = 0; offset + 0x4Cu <= 0x40000u;
         offset += 4u) {
        const std::uint32_t object = 0x02000000u + offset;
        if (ewram32(bus, object) != kFieldLayerVtable)
            continue;
        const unsigned layer = ewram32(bus, object + 4u) & 0xFFu;
        if (layer < 4u &&
            field_object_valid(bus, object, static_cast<int>(layer))) {
            g_field_objects[layer] = object;
        }
    }
}

// Some Webfoot dialogue/cutscene screens install only BG3 as a field layer,
// backed by one 32x32-tile resource chunk. That chunk is a screen-space
// composition, not a scrolling field: extending it through the world-resource
// provider exposes unrelated parts of the chunk in the widescreen margins and
// tears the dialogue panel apart. Keep that scene's BGs in their native
// 240-pixel viewport; the host margins remain transparent/backdrop-colored.
// Larger or multi-layer resources continue through normal field expansion.
bool is_single_chunk_bg3_scene(gba::GbaBus* bus) {
    if (!bus) return false;
    resolve_field_objects(bus);
    if (!g_field_objects[3] || g_field_objects[0] ||
        g_field_objects[1] || g_field_objects[2]) {
        return false;
    }
    const std::uint32_t resource = ewram32(bus, g_field_objects[3] + 0x08u);
    return mem8(bus, resource + 0x14u) == 1u &&
        mem8(bus, resource + 0x15u) == 1u;
}

bool is_overworld(gba::GbaBus* bus) {
    if (!bus || !g_ws_active) return false;
    const std::uint16_t dispcnt = bus->io().read16(0x000u);
    if ((dispcnt & 7u) != 0u || (dispcnt & 0x1F40u) != 0x1F40u)
        return false;

    for (int bg = 0; bg < 4; ++bg) {
        const std::uint16_t cnt =
            bus->io().read16(0x008u + static_cast<std::uint32_t>(bg * 2));
        if (((cnt >> 8) & 0x1Fu) != static_cast<unsigned>(28 + bg) ||
            ((cnt >> 14) & 3u) != 0u) {
            return false;
        }
    }
    return true;
}

bool field_entry_resolve(
    gba::GbaBus* bus, std::uint32_t object, int world_x, int world_y,
    std::uint16_t* out_entry) {
    const std::uint32_t resource = ewram32(bus, object + 0x08u);
    const unsigned chunk_columns = mem8(bus, resource + 0x14u);
    const unsigned chunk_rows = mem8(bus, resource + 0x15u);
    if (chunk_columns == 0u || chunk_rows == 0u)
        return false;

    // The resource's chunk grid is the authored map extent. Each descriptor
    // expands to one 32x32-tile (256x256-pixel) field chunk.
    if (static_cast<unsigned>(world_x) >= chunk_columns * 256u ||
        static_cast<unsigned>(world_y) >= chunk_rows * 256u) {
        return false;
    }

    const unsigned tile_x = static_cast<unsigned>(world_x) >> 3;
    const unsigned tile_y = static_cast<unsigned>(world_y) >> 3;
    const unsigned chunk_x = tile_x >> 5;
    const unsigned chunk_y = tile_y >> 5;
    const unsigned chunk_index = chunk_y * chunk_columns + chunk_x;
    if (chunk_index > 0xFEu)
        return false;

    // The renderer owns four fixed 32x32 physical caches beginning at +0x4C.
    // object+0x20 records which resource chunk currently occupies each slot.
    // The four pointers at +0x10 are instead the current 2x2 screen-quadrant
    // assignment; they rotate and may point at the shared blank map when the
    // camera crosses a resource edge. Pairing an owner byte with the same
    // +0x10 index therefore produces the stale margin seam seen while walking.
    std::uint32_t chunk = 0;
    for (unsigned slot = 0; slot < 4; ++slot) {
        if (mem8(bus, object + 0x20u + slot) == chunk_index) {
            chunk = object + 0x4Cu +
                static_cast<std::uint32_t>(slot) * kFieldChunkBytes;
            break;
        }
    }
    const unsigned local_x = tile_x & 31u;
    const unsigned local_y = tile_y & 31u;
    const std::uint32_t offset =
        static_cast<std::uint32_t>((local_y * 32u + local_x) * 2u);
    if (chunk == 0u) {
        const std::uint32_t descriptor =
            mem32(bus, resource + 0x18u + chunk_index * 4u);
        const std::uint8_t* decoded =
            decoded_field_chunk(bus, descriptor);
        if (!decoded)
            return false;
        *out_entry = static_cast<std::uint16_t>(decoded[offset]) |
            static_cast<std::uint16_t>(decoded[offset + 1u] << 8);
        return true;
    }

    if (!((chunk >= 0x02000000u && chunk + offset + 1u < 0x02040000u) ||
          (chunk >= 0x08000000u &&
           chunk + offset + 1u - 0x08000000u < bus->rom_size()))) {
        return false;
    }
    *out_entry = mem16(bus, chunk + offset);
    return true;
}

// Margin tile-entry memo wrapper (see g_field_tile_cache). Resolving the same
// (layer, world tile) more than once in a composed frame returns the memoized
// result; the range guard stays here so an out-of-range coordinate never
// touches the cache.
bool field_entry(
    gba::GbaBus* bus, std::uint32_t object, int world_x, int world_y,
    std::uint16_t* out_entry) {
    if (!bus || !out_entry || world_x < 0 || world_y < 0)
        return false;
    static const bool cache_enabled = []() {
        const char* v = std::getenv("GBARECOMP_MARGIN_CACHE");
        return !(v && v[0] == '0');
    }();
    if (!cache_enabled)
        return field_entry_resolve(bus, object, world_x, world_y, out_entry);
    const std::int32_t tile_x = static_cast<std::int32_t>(world_x) >> 3;
    const std::int32_t tile_y = static_cast<std::int32_t>(world_y) >> 3;
    const std::uint32_t hash =
        (object * 2654435761u) ^
        (static_cast<std::uint32_t>(tile_x) * 2246822519u) ^
        (static_cast<std::uint32_t>(tile_y) * 3266489917u);
    FieldTileCacheSlot& slot =
        g_field_tile_cache[(hash >> 8) & (kFieldTileCacheSize - 1u)];
    if (slot.generation == g_resolve_frame && slot.object == object &&
        slot.tile_x == tile_x && slot.tile_y == tile_y) {
        if (!slot.valid)
            return false;
        *out_entry = slot.entry;
        return true;
    }
    std::uint16_t entry = 0u;
    const bool ok = field_entry_resolve(bus, object, world_x, world_y, &entry);
    slot.generation = g_resolve_frame;
    slot.object = object;
    slot.tile_x = tile_x;
    slot.tile_y = tile_y;
    slot.entry = entry;
    slot.valid = ok;
    return ok;
}

bool field_screen_entry(
    gba::GbaBus* bus, int bg, int hardware_x, int screen_y,
    std::uint16_t* out_entry) {
    if (!bus || !out_entry || bg < 0 || bg > 3)
        return false;
    resolve_field_objects(bus);
    const std::uint32_t object = g_field_objects[bg];
    if (!object)
        return false;

    // +0x34/+0x38 are the layer's parallax offsets, not the camera.
    // The renderer writes the absolute layer scroll to BGxHOFS/VOFS and
    // records floor((scroll - {8,48}) / 256) at +0x24/+0x28. Recombine
    // those values to recover the authored coordinate rather than using the
    // wrapped 256-pixel hardware ring. The bias disambiguates the interval
    // when the register's low byte lies just before a chunk boundary.
    const int chunk_x = static_cast<std::int32_t>(
        ewram32(bus, object + 0x24u));
    const int chunk_y = static_cast<std::int32_t>(
        ewram32(bus, object + 0x28u));
    const std::uint32_t bg_io =
        0x010u + static_cast<std::uint32_t>(bg * 4);
    const int hofs = bus->io().read16(bg_io) & 0xFF;
    const int vofs = bus->io().read16(bg_io + 2u) & 0xFF;
    const int layer_scroll_x =
        chunk_x * 256 + hofs + (hofs < kFieldCacheBiasX ? 256 : 0);
    const int layer_scroll_y =
        chunk_y * 256 + vofs + (vofs < kFieldCacheBiasY ? 256 : 0);
    return field_entry(
        bus, object, layer_scroll_x + hardware_x,
        layer_scroll_y + screen_y, out_entry);
}

int layer_scroll_x(gba::GbaBus* bus, int bg, std::uint32_t object) {
    const int chunk_x = static_cast<std::int32_t>(
        ewram32(bus, object + 0x24u));
    const std::uint32_t bg_io =
        0x010u + static_cast<std::uint32_t>(bg * 4);
    const int hofs = bus->io().read16(bg_io) & 0xFF;
    return chunk_x * 256 + hofs +
        (hofs < kFieldCacheBiasX ? 256 : 0);
}

// ── Widescreen scene census (development instrumentation) ─────────────
// GBARECOMP_WS_CENSUS=<csv> records, once per composed frame (sampled), how
// the frame classifies and how far every authored field layer extends beyond
// the native 240px viewport at the current scroll. The layer-space reach is
// converted through each layer's parallax scale so the CSV reports real
// revealable screen pixels, independent of the host window width. Used to
// measure widescreen coverage across a full-game TAS replay; fully inert
// unless the environment variable is set. Reads bus state only.
std::FILE* g_census_file = nullptr;
unsigned g_census_interval = 60;
unsigned long long g_census_frame_logged = ~0ull;
unsigned long long g_census_next_sample = 0;
unsigned g_census_signature = ~0u;

// Repeats is_overworld()'s DISPCNT/BG-slot test without its expanded-view
// gate: a native-width census replay never expands the view, but the field
// frames (and their authored extents) are exactly what it must classify.
bool census_field_scene(gba::GbaBus* bus) {
    if (!bus) return false;
    const std::uint16_t dispcnt = bus->io().read16(0x000u);
    if ((dispcnt & 7u) != 0u || (dispcnt & 0x1F40u) != 0x1F40u)
        return false;
    for (int bg = 0; bg < 4; ++bg) {
        const std::uint16_t cnt =
            bus->io().read16(0x008u + static_cast<std::uint32_t>(bg * 2));
        if (((cnt >> 8) & 0x1Fu) != static_cast<unsigned>(28 + bg) ||
            ((cnt >> 14) & 3u) != 0u) {
            return false;
        }
    }
    return true;
}

void census_configure() {
    const char* path = std::getenv("GBARECOMP_WS_CENSUS");
    if (!path || !path[0]) return;
    if (const char* iv = std::getenv("GBARECOMP_WS_CENSUS_INTERVAL")) {
        const int v = std::atoi(iv);
        if (v > 0) g_census_interval = static_cast<unsigned>(v);
    }
    g_census_file = std::fopen(path, "wb");
    if (!g_census_file) {
        std::fprintf(stderr, "ws_census=FAILED to open \"%s\"\n", path);
        return;
    }
    std::fprintf(g_census_file,
        "frame,mode,field,single,layers,cam_x,cam_left,"
        "cam_right,min_left,min_right,"
        "l0_scroll,l0_cols,l0_rows,l1_scroll,l1_cols,l1_rows,"
        "l2_scroll,l2_cols,l2_rows,l3_scroll,l3_cols,l3_rows\n");
    std::fprintf(stderr, "ws_census=ENABLED path=\"%s\" interval=%u\n",
        path, g_census_interval);
}

void census_frame(gba::GbaBus* bus) {
    if (!g_census_file || !bus) return;

    const unsigned long long frame = g_runtime_vblank_starts;
    if (frame == g_census_frame_logged) return;
    g_census_frame_logged = frame;

    const unsigned mode =
        static_cast<unsigned>(bus->io().read16(0x000u) & 7u);
    const bool field = census_field_scene(bus);
    // At native width the wide-path bookkeeping (which advances
    // g_resolve_frame once per composed frame) never runs; advance it here so
    // resolve_field_objects() still allows one fresh EWRAM scan per frame.
    if (field && !g_ws_active)
        g_resolve_frame = static_cast<std::uint32_t>(frame);
    const bool single = field && is_single_chunk_bg3_scene(bus);

    unsigned layers_mask = 0u;
    int min_left = -1;
    int min_right = -1;
    int l_scroll[4] = {-1, -1, -1, -1};
    int l_cols[4] = {0, 0, 0, 0};
    int l_rows[4] = {0, 0, 0, 0};
    if (field) {
        for (int layer = 0; layer < 4; ++layer) {
            const std::uint32_t object = g_field_objects[layer];
            if (!object) continue;
            const std::uint32_t resource = ewram32(bus, object + 0x08u);
            const int cols = mem8(bus, resource + 0x14u);
            const int rows = mem8(bus, resource + 0x15u);
            const int scale_x = static_cast<int>(
                static_cast<std::int32_t>(ewram32(bus, object + 0x3Cu)));
            if (cols <= 0 || rows <= 0 || scale_x <= 0) continue;
            layers_mask |= 1u << layer;
            const int scroll_x = layer_scroll_x(bus, layer, object);
            const int reach_left = scroll_x;
            const int reach_right = cols * 256 - (scroll_x + 240);
            const int left_px = static_cast<int>(
                static_cast<long long>(reach_left) * 1024 / scale_x);
            const int right_px = static_cast<int>(
                static_cast<long long>(reach_right) * 1024 / scale_x);
            l_scroll[layer] = scroll_x;
            l_cols[layer] = cols;
            l_rows[layer] = rows;
            min_left = min_left < 0 ? left_px : std::min(min_left, left_px);
            min_right = min_right < 0 ? right_px
                                      : std::min(min_right, right_px);
        }
    }

    unsigned signature = mode | (field ? 8u : 0u) |
        (single ? 16u : 0u) | (layers_mask << 5);
    for (int layer = 0; layer < 4; ++layer)
        signature = signature * 31u +
            static_cast<unsigned>(l_cols[layer] * 256 + l_rows[layer]);
    if (frame < g_census_next_sample && signature == g_census_signature)
        return;
    g_census_next_sample = frame + g_census_interval;
    g_census_signature = signature;

    const int cam_x = static_cast<std::int32_t>(mem32(bus, kCameraX));
    const int cam_left = static_cast<std::int32_t>(mem32(bus, kCameraLeft));
    const int cam_right = static_cast<std::int32_t>(mem32(bus, kCameraRight));
    std::fprintf(g_census_file,
        "%llu,%u,%d,%d,%u,%d,%d,%d,%d,%d,"
        "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
        frame, mode, field ? 1 : 0, single ? 1 : 0, layers_mask,
        cam_x, cam_left, cam_right, min_left, min_right,
        l_scroll[0], l_cols[0], l_rows[0], l_scroll[1], l_cols[1], l_rows[1],
        l_scroll[2], l_cols[2], l_rows[2], l_scroll[3], l_cols[3], l_rows[3]);
    std::fflush(g_census_file);
}

// NOTE: this provider used to slide the native 240px viewport sideways inside
// the widened host surface ("boundary framing") so the requested window stayed
// inside the authored field. It depended on field_content_x_bounds(), whose
// estimate is refined incrementally across frames and is routinely narrower
// than the real authored extent, so acting on a non-final estimate relocated
// the whole guest composition - field layers, HUD and screen-locked UI
// together - by tens of pixels. In ordinary dialogue cutscenes that showed up
// as a panel shoved off centre even though every layer agreed internally.
//
// The slide is gone: the native viewport always stays centred at
// ox = (host_width - 240) / 2, and the authored margin chunks fill whatever
// the field can genuinely reach. Columns the map does not cover resolve to
// transparent rather than a repeated or clamped tilemap ring. The provider is
// still registered because it owns the once-per-frame bookkeeping below.
extern "C" int buus_fury_bg_x_provider(
    int bg, int output_x, int screen_y, int* out_hardware_x) {
    gba::GbaBus* bus = active_bus();
    if (!out_hardware_x || bg < 0 || bg > 3)
        return 0;

    // Development census: classify each composed frame before the field gate
    // so non-field frames are counted too. The per-frame guard inside
    // census_frame() collapses the per-layer (0,0) visits into one record;
    // it is inert unless GBARECOMP_WS_CENSUS is set.
    if (g_census_file && output_x == 0 && screen_y == 0)
        census_frame(bus);

    if (!is_overworld(bus))
        return 0;

    // Rendering visits BG3 first and begins at output (0,0). Refresh the
    // per-frame framing state once per composed frame rather than rescanning
    // resource bounds in this per-pixel callback.
    if (bg == 3 && output_x == 0 && screen_y == 0) {
        // Once-per-frame bookkeeping, at the first BG3 output pixel.
        // resolve_field_objects() is gated to once per frame, so calling it
        // here is free.
        resolve_field_objects(bus);
        g_single_chunk_bg3_scene = is_single_chunk_bg3_scene(bus);
        ++g_resolve_frame;
        g_previous_obj_x = g_current_obj_x;
        g_current_obj_x = {};
        if (++g_obj_x_generation == 0) {
            g_previous_obj_x = {};
            g_current_obj_x = {};
            g_actor_history = {};
            g_obj_x_generation = 1;
        }
    }

    // Never displace a layer horizontally. The compositor already centres the
    // native viewport and resolves field margins through the tilemap provider,
    // so the guest's own hardware_x stays authoritative for every BG.
    (void)bg;
    return 0;
}

// ── Full-screen menu margins ───────────────────────────────────────────
// The pause-menu family (Status/Journal/Inventory/Skills) paints its whole
// 240x160 screen as a single 256-color BG2 art layer over a one-pixel
// horizontal stripe background (palette indices 1 and 48 alternating per
// scanline). The stripes are x-independent, so the widescreen margins
// continue seamlessly by donating the matching tile row from the art's
// leftmost tile column (x=0..7), which never carries content on these
// screens. The donor tile is decoded and validated to contain only the two
// background indices before use; any other screen that merely matches the
// register shape fails closed to the transparent pillarbox.
bool menu_stripe_scene(gba::GbaBus* bus) {
    if (!bus) return false;
    const std::uint16_t dispcnt = bus->io().read16(0x000u);
    if ((dispcnt & 7u) != 0u || (dispcnt & 0x0F00u) != 0x0400u)
        return false;
    const std::uint16_t cnt = bus->io().read16(0x00Cu);
    if ((cnt & 0x0080u) == 0u || ((cnt >> 14) & 3u) != 0u)
        return false;
    return bus->io().read16(0x018u) == 0u &&
        bus->io().read16(0x01Au) == 0u;
}

std::uint32_t g_menu_donor_frame = std::numeric_limits<std::uint32_t>::max();
std::uint32_t g_menu_donor_rows = 0u;
std::uint16_t g_menu_donor_entry[20] = {};

bool menu_margin_entry(gba::GbaBus* bus, int screen_y, std::uint16_t* out) {
    if (!bus || !out || screen_y < 0 || screen_y >= 160)
        return false;
    const unsigned row = static_cast<unsigned>(screen_y) >> 3;
    if (g_menu_donor_frame != g_resolve_frame) {
        g_menu_donor_frame = g_resolve_frame;
        g_menu_donor_rows = 0u;
    }
    if ((g_menu_donor_rows & (1u << row)) == 0u) {
        const std::uint16_t cnt = bus->io().read16(0x00Cu);
        const std::uint32_t char_base = ((cnt >> 2) & 3u) * 0x4000u;
        const std::uint32_t screen_base = ((cnt >> 8) & 0x1Fu) * 0x800u;
        const std::uint32_t map_off = screen_base +
            static_cast<std::uint32_t>(row * 32u) * 2u;
        const std::uint8_t* vram = bus->vram_ptr();
        if (map_off + 1u < 96u * 1024u) {
            const std::uint16_t entry =
                static_cast<std::uint16_t>(vram[map_off]) |
                (static_cast<std::uint16_t>(vram[map_off + 1u]) << 8);
            const std::uint32_t tile =
                char_base + (entry & 0x3FFu) * 64u;
            bool pure = tile + 63u < 96u * 1024u;
            for (std::uint32_t i = 0; pure && i < 64u; ++i) {
                const std::uint8_t idx = vram[tile + i];
                if (idx != 1u && idx != 48u) pure = false;
            }
            if (pure) {
                g_menu_donor_entry[row] = entry;
                g_menu_donor_rows |= 1u << row;
            }
        }
    }
    if ((g_menu_donor_rows & (1u << row)) == 0u)
        return false;
    *out = g_menu_donor_entry[row];
    return true;
}

extern "C" int buus_fury_tilemap_provider(
    int bg, int hardware_x, int screen_y, std::uint16_t* out_entry) {
    gba::GbaBus* bus = active_bus();
    if (!out_entry || bg < 0 || bg > 3)
        return gba::kWsTilemapUnavailable;

    if (!is_overworld(bus)) {
        if (bg == 2 && menu_stripe_scene(bus) &&
            menu_margin_entry(bus, screen_y, out_entry))
            return gba::kWsTilemapReplace;
        return gba::kWsTilemapUnavailable;
    }

    if (g_single_chunk_bg3_scene)
        return gba::kWsTilemapUnavailable;

    if (!field_screen_entry(bus, bg, hardware_x, screen_y, out_entry))
        return gba::kWsTilemapUnavailable;
    return gba::kWsTilemapReplace;
}

extern "C" int buus_fury_obj_x_provider(
    int oam_index, std::uint16_t attr0, std::uint16_t attr1,
    std::uint16_t attr2, int* out_x) {
    gba::GbaBus* bus = active_bus();
    if (!out_x || !is_overworld(bus)) return 0;
    if (g_single_chunk_bg3_scene) return 0;

    const int x = attr1 & 0x1FFu;
    const int y = attr0 & 0xFFu;

    // Buu's Fury retains the Webfoot convention in which the first three OAM
    // slots are the top-left portrait/health/energy group. World actors begin
    // at slot 3 and must stay camera-relative.
    //
    // Dialogue panel art reuses these low slots too (a top panel occupied
    // slots 0..2 at y=8 in the dr capture), so slot/index alone cannot
    // classify the group. The tile bank can: the status group always draws
    // from the static upper OBJ bank (tiles 0x370/0x380/0x388, priority 1),
    // while panel art draws from the per-panel bank at 0x180-0x320 and world
    // actors from the dynamic 0xF00+ bank at priority 3. This is why the old
    // slot-only test tore top panels in half when the group was displaced by
    // a margin: it claimed panel pieces as status pieces.
    //
    // Edge anchoring: in expanded views the status group leaves the centred
    // native viewport and pins to the physical left edge. The compositor
    // re-adds the left margin to every resolved sprite X, so subtracting the
    // margin here lands the group at output x = native x (the true screen
    // edge). At native width g_ws_extra_left is 0 and every coordinate stays
    // byte-for-byte native.
    if (oam_index >= 0 && oam_index <= 2 && x < 96 && y < 32) {
        const unsigned tile = attr2 & 0x3FFu;
        const unsigned priority = (attr2 >> 10) & 3u;
        if (tile >= 0x360u && tile < 0x400u && priority == 1u) {
            *out_x = x - static_cast<int>(g_ws_extra_left);
            return 1;
        }
        *out_x = x;
        return 1;
    }

    // Expanded views make OAM X=256..511 ambiguous: those values may be
    // genuine coordinates in the new right margin or hardware-wrapped
    // negative coordinates leaving through the left edge.
    const int widened_right = 240 + static_cast<int>(g_ws_extra_right);
    int world_x = x <= widened_right ? x : x - 512;
    const int raw_y = y >= 160 ? y - 256 : y;

    // Only raw OAM X values with bit 8 set (256..511) are actually
    // ambiguous on real hardware: those are the only values a native
    // 240px game can legitimately use to mean "just off the left edge"
    // via 9-bit signed wraparound. Anything below 256 is an ordinary,
    // already-correct on-screen X with nothing left to resolve --
    // crucially this includes every screen-locked UI graphic (the
    // dialogue window, portrait box, etc.), which has no reason to ever
    // use the wraparound trick. Restricting actor/continuity matching to
    // only the genuinely ambiguous values means static UI can no longer
    // be "claimed" by a nearby world actor merely because both happened
    // to pass through the same generic resolution path. This is the fix
    // for the dialogue box tearing apart near the player: previously
    // EVERY non-icon sprite -- including one with a perfectly
    // unambiguous X -- was run through actor/history matching, and if
    // that first, uncorrected frame produced a wrong answer (e.g. the
    // dialogue box's own first frame, when it still had no history and
    // the player happened to be nearby), the wrong position then got
    // written into history and kept re-confirming itself every frame
    // after, which is why reordering match priority alone did not fix
    // it: the corruption was already baked in before history existed.
    bool history_matched = false;
    if (x >= 256) {
        // Resolve against this OAM piece's OWN previous-frame continuity
        // first, and only fall back to nearest-live-actor proximity when
        // nothing matches. The renderer compacts surviving pieces into
        // different OAM slots as a composite actor leaves, so this check
        // matches piece attributes across the complete previous frame
        // rather than assuming an OAM index is a persistent identity.
        if (oam_index >= 0 && oam_index < 128) {
            int best_distance = std::numeric_limits<int>::max();
            int best_x = world_x;
            for (const ObjXHistory& history : g_previous_obj_x) {
                const bool same_piece =
                    history.valid &&
                    history.attr0_shape == (attr0 & 0xFF00u) &&
                    history.attr1_shape == (attr1 & 0xFE00u) &&
                    history.attr2 == attr2 &&
                    distance(history.raw_y, raw_y) <= 8;
                if (!same_piece) continue;

                const int candidates[] = {x, x - 512};
                for (const int candidate : candidates) {
                    const int dx = distance(candidate, history.resolved_x);
                    if (dx < best_distance) {
                        best_distance = dx;
                        best_x = candidate;
                    }
                }
            }
            if (best_distance != std::numeric_limits<int>::max()) {
                world_x = best_x;
                history_matched = true;
            }
        }
        // Only a piece with no continuity at all (its genuine first frame
        // on screen, and one whose X was genuinely ambiguous) falls back
        // to the fuzzy nearest-live-actor guess.
        if (!history_matched)
            match_actor_x(bus, x, raw_y, &world_x);
    }

    if (oam_index >= 0 && oam_index < 128) {
        ObjXHistory& history =
            g_current_obj_x[static_cast<std::size_t>(oam_index)];
        history.attr0_shape = attr0 & 0xFF00u;
        history.attr1_shape = attr1 & 0xFE00u;
        history.attr2 = attr2;
        history.raw_y = raw_y;
        history.resolved_x = world_x;
        history.valid = true;
    }
    *out_x = world_x;
    return 1;
}

// Movement turbo bookkeeping, refreshed at the game's single keypad poll
// (once per frame). Turbo is armed only while L is held during walkable field
// gameplay, and resolves the controlled hero through the game's own global so
// no object is ever scaled by guesswork. L is the free field key: holding
// Select, R, B or Start suppresses native walking (each has its own game
// function), while L+Right walks exactly like Right and L alone is
// pixel-transparent (all verified with TCP keypad + frame compares).
void update_turbo(gba::GbaBus* bus, bool walkable_field, bool l_held) {
    g_turbo_active = false;
    if (!bus || !walkable_field || !l_held) {
        g_turbo_player = 0u;
        return;
    }
    const std::uint32_t player = mem32(bus, kControlledActor);
    if (player < 0x02000000u ||
        player + kActorFixedY + 4u > 0x02040000u) {
        g_turbo_player = 0u;
        return;
    }
    const std::uint32_t vtable = mem32(bus, player);
    if (vtable < 0x08000000u || vtable >= 0x0A000000u) {
        g_turbo_player = 0u;
        return;
    }
    g_turbo_player = player;
    g_turbo_active = true;
}

// ── Sound test / music player ──────────────────────────────────────────
// The game's music engine resolves a song request through the service at
// 0x080008A8 and arms the master streaming state at 0x030038A4 (the record
// pointer it is playing lives at +0xC = 0x030038B0). The record table is at
// 0x083BB78C: 53 records of 20 bytes, {stream_a, stream_b, bank, bank,
// length}. 0x0803F110(state, record, old_record, index*4) is the engine's own
// "start this record now" call, captured live at the title->field music
// switch (the service passes the previous record for the crossfade). The mod
// replays that exact call with the generated-call ABI: push a return frame,
// set the AAPCS argument registers, dispatch natively, restore the
// caller-visible register file. Nothing here runs unless the player presses
// the chord, so regression frames stay byte-identical.
constexpr std::uint32_t kMusicState = 0x030038A4u;
constexpr std::uint32_t kMusicRecordPtr = 0x030038B0u;
constexpr std::uint32_t kSongTable = 0x083BB78Cu;
constexpr std::uint32_t kSongCount = 53u;
constexpr std::uint32_t kSongRecordBytes = 20u;
constexpr std::uint32_t kStartSongFn = 0x0803F110u;
// Return address that is never executed: runtime_call_should_return() pops
// the frame and the generated tail dispatch returns to the host caller
// instead of dispatching it.
constexpr std::uint32_t kSongReturnSentinel = 0x03007FF0u;
int g_song_cycle_request = 0;
std::uint32_t g_song_prev_keys = 0x3FFu;
int g_song_last_index = -1;

void call_start_song(std::uint32_t state, std::uint32_t record,
                     std::uint32_t old_record, std::uint32_t index4) {
    std::uint32_t saved_regs[16];
    for (int i = 0; i < 16; ++i) saved_regs[i] = g_cpu.R[i];
    const std::uint32_t saved_cpsr = g_cpu.cpsr;

    const std::uint32_t depth_before = runtime_call_stack_depth();
    runtime_call_push_return(kSongReturnSentinel | 1u);
    g_cpu.R[0] = state;
    g_cpu.R[1] = record;
    g_cpu.R[2] = old_record;
    g_cpu.R[3] = index4;
    g_cpu.R[14] = kSongReturnSentinel | 1u;
    runtime_dispatch_with_exchange(kStartSongFn | 1u);
    if (runtime_call_stack_depth() > depth_before)
        runtime_call_cancel_return(kSongReturnSentinel | 1u);

    for (int i = 0; i < 16; ++i) g_cpu.R[i] = saved_regs[i];
    g_cpu.cpsr = saved_cpsr;
}

// Chord detection at the game's single keypad poll (once per frame): L+R
// held plus a fresh Left/Right press cycles the current record. Walkable
// field gameplay is excluded (L is movement turbo and R blocks there);
// menus, dialogues and cutscenes all work.
void update_sound_test_keys(std::uint32_t keys, bool walkable_field) {
    const bool chord =
        !walkable_field && (keys & 0x200u) == 0u && (keys & 0x100u) == 0u;
    if (chord) {
        if ((keys & 0x20u) == 0u && (g_song_prev_keys & 0x20u) != 0u)
            g_song_cycle_request = -1;
        else if ((keys & 0x10u) == 0u && (g_song_prev_keys & 0x10u) != 0u)
            g_song_cycle_request = 1;
    }
    g_song_prev_keys = keys;
}

// Serviced from the keypad-poll read override the moment a cycle request is
// set: that context has the game's own return frames open, which is exactly
// what the generated call/return ABI needs.
void sound_test_service(gba::GbaBus* bus) {
    if (!bus || g_song_cycle_request == 0)
        return;
    const int request = g_song_cycle_request;
    g_song_cycle_request = 0;

    const std::uint32_t current = mem32(bus, kMusicRecordPtr);
    int index = -1;
    if (current >= kSongTable &&
        current < kSongTable + kSongCount * kSongRecordBytes &&
        ((current - kSongTable) % kSongRecordBytes) == 0u) {
        index = static_cast<int>((current - kSongTable) / kSongRecordBytes);
    } else if (g_song_last_index >= 0) {
        index = g_song_last_index;
    } else {
        index = 0;
    }
    int next = index + request;
    if (next < 0) next = static_cast<int>(kSongCount) - 1;
    if (next >= static_cast<int>(kSongCount)) next = 0;
    g_song_last_index = next;
    call_start_song(
        kMusicState,
        kSongTable + static_cast<std::uint32_t>(next) * kSongRecordBytes,
        current, static_cast<std::uint32_t>(next) * 4u);
}

// ── Optional West City waypoint (opt-in DataCrystal data port) ─────────
// West City's waypoint list is a ROM table: a u32 count at 0x0826A7B8, a
// u32 pointer to an array of u32 template pointers at 0x0826A7BC, and 24-
// byte waypoint templates (two constructor pointers + four packed
// attribute words). The stock table lists two waypoints (Capsule Corp and
// Z-Mart). The DataCrystal "Add a waypoint" hack bumps the count to 3 and
// points the array at free ROM space (0x087E6A00), where it re-lists the
// two stock templates plus a new one built from the game's own constructor
// pair (0x08007674/0x0800776C) with West City coordinates. Ported without
// touching the ROM: when enabled, the read override serves the hacked
// values to any guest read of the three regions. Off unless
// GBARECOMP_BF_WAYPOINTS=1; the guest never reads those addresses in
// states without West City's map, so enabling it cannot affect them.
constexpr std::uint32_t kWaypointCountAddr = 0x0826A7B8u;
constexpr std::uint32_t kWaypointArrayAddr = 0x0826A7BCu;
constexpr std::uint32_t kWaypointPatchAddr = 0x087E6A00u;
constexpr std::size_t kWaypointPatchBytes = 36u;
constexpr std::uint8_t kWaypointPatch[kWaypointPatchBytes] = {
    0x40, 0xA7, 0x26, 0x08,  // 0x0826A740: Capsule Corp template
    0x58, 0xA7, 0x26, 0x08,  // 0x0826A758: Z-Mart template
    0x0C, 0x6A, 0x7E, 0x08,  // 0x087E6A0C: the new template below
    0x75, 0x76, 0x00, 0x08,  // 0x08007675: game's waypoint constructor
    0x6D, 0x77, 0x00, 0x08,  // 0x0800776D: game's waypoint constructor
    0x08, 0x01, 0x40, 0x00,  // 0x00400108
    0x11, 0x01, 0x50, 0x00,  // 0x00500111
    0x01, 0x02, 0x02, 0x00,  // 0x00020201
    0xD8, 0x00, 0xAC, 0x01,  // 0x01AC00D8
};
bool g_waypoint_feature = false;

bool waypoint_patch_read(
    std::uint32_t address, std::uint32_t width, std::uint32_t* out_value) {
    if (!out_value || (width != 1u && width != 2u && width != 4u))
        return false;
    if (address == kWaypointCountAddr && width == 4u) {
        *out_value = 3u;
        return true;
    }
    if (address == kWaypointArrayAddr && width == 4u) {
        *out_value = kWaypointPatchAddr;
        return true;
    }
    if (address >= kWaypointPatchAddr &&
        address + width <= kWaypointPatchAddr + kWaypointPatchBytes) {
        std::uint32_t value = 0u;
        const std::size_t offset = address - kWaypointPatchAddr;
        for (std::uint32_t i = 0u; i < width; ++i)
            value |= static_cast<std::uint32_t>(
                kWaypointPatch[offset + i]) << (8u * i);
        *out_value = value;
        return true;
    }
    return false;
}

extern "C" int buus_fury_bus_write_override(
    std::uint32_t pc, std::uint32_t address, std::uint32_t width,
    std::uint32_t value, std::uint32_t* out_value) {
    (void)pc;
    if (!out_value || !g_turbo_active || width != 4u)
        return 0;
    const std::uint32_t player = g_turbo_player;
    if (address != player + kActorFixedX && address != player + kActorFixedY)
        return 0;
    gba::GbaBus* bus = active_bus();
    if (!bus)
        return 0;
    const std::int32_t before = static_cast<std::int32_t>(mem32(bus, address));
    const std::int32_t delta = static_cast<std::int32_t>(value) - before;
    const std::int32_t magnitude = delta < 0 ? -delta : delta;
    // Scale only an ordinary per-frame walking step. Idle re-writes (delta 0)
    // and teleports/respawns/area loads (large jumps) stay exactly native.
    if (magnitude == 0 || magnitude > kWalkStepLimit)
        return 0;
    *out_value = static_cast<std::uint32_t>(before + delta + delta / 2);
    return 1;
}

extern "C" int buus_fury_bus_read_override(
    std::uint32_t pc, std::uint32_t address, std::uint32_t width,
    std::uint32_t original, std::uint32_t* out_value) {
    gba::GbaBus* bus = active_bus();
    if (g_waypoint_feature && out_value &&
        waypoint_patch_read(address, width, out_value))
        return 1;
    // Census trigger: the first guest bus read of a frame classifies it.
    // This fires at native width too (the margin providers never run there),
    // which is what lets a full-game census replay at full speed.
    if (g_census_file) census_frame(bus);
    // Player comfort: holding Select auto-taps the button the current screen
    // expects. Cinematic single-chunk screens advance with B (the intro battle
    // and story cutscenes scroll when B is tapped and ignore A, verified with
    // replays); other non-field screens advance with A (menus, prompts).
    // Walkable field gameplay is excluded entirely: the game itself consumes
    // Select there (holding it suppresses walking), so the auto-tap stays out
    // of the field and can never fire attacks while walking. The tap is a
    // two-frame window every twenty frames: the game advances on fresh press
    // edges, and a continuously held button only registers once. KEYINPUT is
    // active-low (bit 0 = A, bit 1 = B, bit 2 = Select) and 0x0803D4A6 is the
    // game's single keypad poll site reading 0x04000130.
    if (out_value && bus && address == 0x04000130u && width == 2u) {
        const bool walkable_field =
            is_overworld(bus) && !g_single_chunk_bg3_scene;
        // Once-per-frame bookkeeping: refresh the movement-turbo target. In
        // walkable field gameplay a held L boosts the hero's walking step
        // 1.5x through the write override; menus and cutscenes keep the
        // auto-advance behavior below untouched (that one keys on Select).
        update_turbo(bus, walkable_field, (original & 0x200u) == 0u);
        update_sound_test_keys(original, walkable_field);
        // Service the music-cycle request right here: the keypad poll runs
        // with the game's own return frames open, which is the clean context
        // for a guest call (the generated call/return ABI needs the pushed
        // frame to be consumed by the callee's pop{pc}).
        if (g_song_cycle_request != 0)
            sound_test_service(bus);
        if ((original & 0x004u) == 0u && !walkable_field &&
            (g_runtime_vblank_starts % 20ull) < 2ull) {
            const std::uint32_t button =
                g_single_chunk_bg3_scene ? 0x0002u : 0x0001u;
            *out_value = original & ~button;
            return 1;
        }
    }
    if (!out_value || width != 4u || !is_overworld(bus))
        return 0;
    if (g_single_chunk_bg3_scene)
        return 0;

    // The actor renderer intersects each object's world rectangle with the
    // camera rectangle at 0x030019B[C..C8]. 0x08010192 performs separate
    // tests for the main meta-sprite and its auxiliary OAM piece; both must
    // use the widened bounds or composite NPCs visibly shed pieces at the
    // edge. Retain the older generic-object path for other overworld scenes.
    const bool visibility_left =
        pc == 0x08019C06u ||
        pc == 0x08010216u ||
        pc == 0x0801029Cu;
    const bool visibility_right =
        pc == 0x08019BFEu ||
        pc == 0x08010210u ||
        pc == 0x08010292u;
    if (!visibility_left && !visibility_right)
        return 0;

    remember_actor(bus, g_cpu.R[4]);

    // Widen only the visibility bounds the actor renderer uses, leaving the
    // camera, streaming and actor coordinates byte-for-byte native. The native
    // viewport is always centred, so the margins are symmetric extra reach.
    if (visibility_left && address == kCameraLeft) {
        const int extension = kActorCullPadding;
        // Clamp the left bound at 0 instead of subtracting into uint32
        // underflow. At the world's left edge (the start of a new game) the
        // native camera rect begins near 0; a wrapped bound makes the actor
        // renderer believe every piece spans a huge rectangle.
        const std::int32_t original_i = static_cast<std::int32_t>(original);
        *out_value = original_i > extension
            ? static_cast<std::uint32_t>(original_i - extension) : 0u;
        return 1;
    }
    if (visibility_right && address == kCameraRight) {
        const int extension = kActorCullPadding;
        *out_value = original + static_cast<std::uint32_t>(extension);
        return 1;
    }
    return 0;
}

}  // namespace

void install_extended_view(std::uint32_t, std::uint32_t) {
    census_configure();
    for (std::uint32_t& object : g_field_objects)
        object = 0u;
    g_decoded_chunks.clear();
    g_resolve_frame = 0;
    g_last_scan_frame = std::numeric_limits<std::uint32_t>::max();
    g_previous_obj_x = {};
    g_current_obj_x = {};
    g_actor_history = {};
    g_actor_history_cursor = 0;
    g_obj_x_generation = 1;
    gba::g_ws_tilemap_provider = &buus_fury_tilemap_provider;
    gba::g_ws_authored_margin_layers = 1;
    gba::g_ws_bg_x_provider = &buus_fury_bg_x_provider;
    gba::g_ws_bg_x_provider_layers =
        (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3);
    gba::g_ws_obj_attr_x_provider = &buus_fury_obj_x_provider;
    g_runtime_bus_read_override = &buus_fury_bus_read_override;
    g_runtime_bus_write_override = &buus_fury_bus_write_override;
    g_turbo_active = false;
    g_turbo_player = 0u;
    g_song_cycle_request = 0;
    g_song_prev_keys = 0x3FFu;
    g_song_last_index = -1;
    g_menu_donor_frame = std::numeric_limits<std::uint32_t>::max();
    g_menu_donor_rows = 0u;
    const char* waypoints = std::getenv("GBARECOMP_BF_WAYPOINTS");
    g_waypoint_feature = waypoints && waypoints[0] && waypoints[0] != '0';
    if (std::getenv("GBARECOMP_BF_WAYPOINTS_SELFTEST")) {
        std::uint32_t value = 0u;
        const bool count_ok =
            waypoint_patch_read(kWaypointCountAddr, 4u, &value);
        std::fprintf(stderr,
            "[buus-fury:waypoint] selftest count ok=%d value=%u\n",
            count_ok ? 1 : 0, value);
        const bool array_ok =
            waypoint_patch_read(kWaypointArrayAddr, 4u, &value);
        std::fprintf(stderr,
            "[buus-fury:waypoint] selftest array ok=%d value=0x%08X\n",
            array_ok ? 1 : 0, value);
        for (std::uint32_t off = 0u; off < kWaypointPatchBytes; off += 4u) {
            const bool word_ok = waypoint_patch_read(
                kWaypointPatchAddr + off, 4u, &value);
            std::fprintf(stderr,
                "[buus-fury:waypoint] selftest patch+%u ok=%d value=0x%08X\n",
                off, word_ok ? 1 : 0, value);
        }
        const bool byte_ok = waypoint_patch_read(
            kWaypointPatchAddr + 12u, 2u, &value);
        std::fprintf(stderr,
            "[buus-fury:waypoint] selftest u16@+12 ok=%d value=0x%04X\n",
            byte_ok ? 1 : 0, value);
        const bool miss_ok = !waypoint_patch_read(
            kWaypointPatchAddr + kWaypointPatchBytes, 4u, &value);
        std::fprintf(stderr,
            "[buus-fury:waypoint] selftest miss rejected=%d\n",
            miss_ok ? 1 : 0);
    }
    std::fprintf(stderr,
        "[buus-fury:view] authored BG0..BG3 field continuation + centred native "
        "viewport + exact chunk decode + edge HUD + expanded actor "
        "visibility + Select auto-advance + L-held movement turbo + "
        "L+R+arrows music player + full-screen menu margins enabled%s\n",
        g_waypoint_feature ? " + West City waypoint" : "");
}

}  // namespace buus_fury
