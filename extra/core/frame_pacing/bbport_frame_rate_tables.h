#pragma once

#include <cstdint>

namespace BbFrameRate {

enum class Kind : std::uint8_t {
    Step,   // dt (1 / fps)
    Amount, // was * 30 * dt
    Decay,  // 1 - (1 - was)^(30 * dt)
    Keep,   // was^(30 * dt)
    Rate,   // was / (30 * dt)
};

struct Constant {
    std::uint32_t at;
    std::uint32_t was;
    std::uint32_t now;
};

struct Rated {
    std::uint32_t at;
    Kind kind;
    bool each_frame;
};

enum class Count : std::uint8_t {
    Frames,
    Micros,
    Digits,
};

struct Counted {
    std::uint32_t at;
    std::uint8_t n;
    std::uint32_t was;
    Count kind;
};

struct Bytes {
    std::uint32_t at;
    std::uint8_t n;
    std::uint8_t now[10];
};

struct Site {
    std::uint32_t at;
    const char* now;
};

struct DtLoad {
    std::uint32_t insn;
    std::uint8_t len;
    std::uint8_t disp;
    std::uint32_t constant;
    Kind kind;
    std::uint8_t lanes;
    float was;
    const char* bytes;
};

inline constexpr Constant kConstants[] = {
    {0x0a3fc3f, 0x3d088889, 0x3c888889}, {0x0f383ca, 0x3d088889, 0x3c888889},
    {0x12b5458, 0x3d088889, 0x3c888889}, {0x12b55b6, 0x3d088889, 0x3c888889},
    {0x13734f3, 0x3d088889, 0x3c888889}, {0x13ef51c, 0x41f00000, 0x42700000},
    {0x13ef6ec, 0x41f00000, 0x42700000}, {0x16efe5c, 0x3d088889, 0x3c888889},
    {0x183a4c3, 0x3dcccccd, 0x3d4ccccd}, {0x183a4cd, 0x3dcccccd, 0x3d4ccccd},
    {0x183a4d7, 0x3e99999a, 0x3d4ccccd}, {0x183a4e1, 0x3dcccccd, 0x3d4ccccd},
    {0x183a4f5, 0x3dcccccd, 0x3d4ccccd}, {0x183a4ff, 0x3dcccccd, 0x3d4ccccd},
    {0x183a509, 0x3cf5c28f, 0x3c75c28f}, {0x183a527, 0x3dcccccd, 0x3e4ccccd},
    {0x183a531, 0x3e99999a, 0x3e4ccccd}, {0x183a53b, 0x3e99999a, 0x3e19999a},
    {0x183a649, 0x3f860a92, 0x3f32b8c2}, {0x183a653, 0x3d75c28f, 0x3d23d70a},
    {0x183a65d, 0x3dcccccd, 0x3e19999a}, {0x183a8c1, 0x3dcccccd, 0x3d4ccccd},
    {0x183a8cb, 0x3e800000, 0x3d4ccccd}, {0x183a8d5, 0x3e99999a, 0x3d4ccccd},
    {0x183a8df, 0x3e99999a, 0x3d4ccccd}, {0x183a8f3, 0x3dcccccd, 0x3d4ccccd},
    {0x183a8fd, 0x3e800000, 0x3d4ccccd}, {0x183a907, 0x3cf5c28f, 0x3c75c28f},
    {0x183a925, 0x3ecccccd, 0x3e4ccccd}, {0x183a92f, 0x3ecccccd, 0x3e4ccccd},
    {0x183a939, 0x3e99999a, 0x3e19999a}, {0x183aa47, 0x3f860a92, 0x3f32b8c2},
    {0x183aa51, 0x3d75c28f, 0x3d23d70a}, {0x183aa5b, 0x3e99999a, 0x3e19999a},
    {0x1972a80, 0x3d088889, 0x3c888889}, {0x1ab67ef, 0x41f00000, 0x42700000},
    {0x1bf9b74, 0x3d088889, 0x3c888889}, {0x1c0ca9a, 0x3d088889, 0x3c888889},
    {0x1daa65e, 0x3d088889, 0x3c888889}, {0x1ea4003, 0x3d088889, 0x3c888889},
    {0x1ea40a0, 0x3d088889, 0x3c888889}, {0x1ea4146, 0x3d088889, 0x3c888889},
    {0x1ea49ce, 0x3d088889, 0x3c888889}, {0x1ea4bda, 0x3d088889, 0x3c888889},
    {0x1ea5248, 0x3d088889, 0x3c888889}, {0x1ea5476, 0x3d088889, 0x3c888889},
    {0x1ea552d, 0x3d088889, 0x3c888889}, {0x1ea57c0, 0x3d088889, 0x3c888889},
    {0x1ea5ac5, 0x3d088889, 0x3c888889}, {0x1ea6015, 0x3d088889, 0x3c888889},
    {0x1ea6045, 0x3d088889, 0x3c888889}, {0x1ea6275, 0x3d088889, 0x3c888889},
    {0x1ea62a5, 0x3d088889, 0x3c888889}, {0x1ea632f, 0x3d088889, 0x3c888889},
    {0x1eb540b, 0x3d088889, 0x3c888889}, {0x1eb54dc, 0x3d088889, 0x3c888889},
    {0x1eb90aa, 0x3d088889, 0x3c888889}, {0x1eb9117, 0x3d088889, 0x3c888889},
    {0x1ebb111, 0x3d088889, 0x3c888889}, {0x1ebf0d3, 0x3d088889, 0x3c888889},
    {0x1ec3f69, 0x3d088889, 0x3c888889}, {0x1ec3f9a, 0x3d088889, 0x3c888889},
    {0x1ec3fd8, 0x3d088889, 0x3c888889}, {0x1ec4009, 0x3d088889, 0x3c888889},
    {0x1ec404e, 0x3d088889, 0x3c888889}, {0x1ec40a4, 0x3d088889, 0x3c888889},
    {0x1ec40e0, 0x3d088889, 0x3c888889}, {0x1ec4136, 0x3d088889, 0x3c888889},
    {0x1ec4167, 0x3d088889, 0x3c888889}, {0x1ec41a5, 0x3d088889, 0x3c888889},
    {0x1ec41dc, 0x3d088889, 0x3c888889}, {0x1f032f2, 0x3d088889, 0x3c888889},
    {0x1f03762, 0x3d088889, 0x3c888889}, {0x1f6aa32, 0x3d088889, 0x3c888889},
    {0x1f96d9a, 0x3d088889, 0x3c888889}, {0x200d8e2, 0x3d088889, 0x3c888889},
    {0x200e142, 0x3d088889, 0x3c888889}, {0x200e2d4, 0x3d088889, 0x3c888889},
    {0x200fa52, 0x3d088889, 0x3c888889}, {0x20119d2, 0x3d088889, 0x3c888889},
    {0x2012652, 0x3d088889, 0x3c888889}, {0x20127cc, 0x3d088889, 0x3c888889},
    {0x2012932, 0x3d088889, 0x3c888889}, {0x2012ac4, 0x3d088889, 0x3c888889},
    {0x2012c32, 0x3d088889, 0x3c888889}, {0x203acb7, 0x41f00000, 0x42700000},
    {0x222bcd7, 0x3d088889, 0x3c888889}, {0x23dd026, 0x41f00000, 0x42700000},
    {0x23dede6, 0x41f00000, 0x42700000}, {0x2418e39, 0x3d088889, 0x3c888889},
    {0x2418f38, 0x3d088889, 0x3c888889}, {0x243460c, 0x3d088889, 0x3c888889},
    {0x243485f, 0x3d088889, 0x3c888889}, {0x2434883, 0x3d088889, 0x3c888889},
    {0x24348b8, 0x3d088889, 0x3c888889}, {0x25df519, 0x3c888889, 0x3c088889},
    {0x4befdcc, 0x3d088889, 0x3c888889}, {0x4c9f358, 0x41f00000, 0x42700000},
    {0x4cad65c, 0x3d088889, 0x3c888889}, {0x4cad660, 0x3d088889, 0x3c888889},
    {0x4cad664, 0x3d088889, 0x3c888889}, {0x4ccc3f4, 0x3d088889, 0x3c888889},
    {0x4cf6420, 0x3dcccccd, 0x3d4ccccd}, {0x4cf6424, 0x3dcccccd, 0x3d4ccccd},
    {0x4cf6428, 0x3dcccccd, 0x3d4ccccd}, {0x4cf642c, 0x3dcccccd, 0x3d4ccccd},
    {0x4d2577c, 0x3d088889, 0x3c888889}, {0x4d25e68, 0x3dcccccd, 0x3d523176},
    {0x4d25e94, 0x3d088889, 0x3c89b0c3}, {0x4d265fc, 0x3d088889, 0x3c888889},
    {0x4d26618, 0x3d088889, 0x3c888889}, {0x4d27554, 0x3d088889, 0x3c888889},
    {0x4d275f4, 0x3d088889, 0x3c888889}, {0x4d27c4c, 0x3d088889, 0x3c888889},
    {0x4d28e98, 0x3d088889, 0x3c888889}, {0x4d29170, 0x3d088889, 0x3c888889},
    {0x4d29b80, 0x3d088889, 0x3c888889}, {0x4d29e74, 0x3d088889, 0x3c888889},
};

inline constexpr Rated kRated[] = {
    {0x0f383ca, Kind::Step, false},
    {0x1972a80, Kind::Step, false},
    {0x1c0ca9a, Kind::Step, false},
    {0x25df519, Kind::Amount, false},
    {0x4cf6420, Kind::Amount, false},
    {0x4cf6424, Kind::Amount, false},
    {0x4cf6428, Kind::Amount, false},
    {0x4cf642c, Kind::Amount, false},
    {0x4d25e68, Kind::Decay, true},
    {0x4d25e94, Kind::Decay, true},
};

inline constexpr Counted kCounts[] = {
    {0x192c405, 4, 10, Count::Frames},
    {0x1929676, 4, 5, Count::Frames},
    {0x25629f1, 4, 33000, Count::Micros},
    {0x4b33c51, 5, 33333, Count::Digits},
};

inline constexpr Bytes kBytes[] = {
    {0x2434863, 10, {0x48, 0xb9, 0, 0, 0, 0, 0, 0, 0, 0}},
    {0x2434887, 10, {0x48, 0xb9, 0, 0, 0, 0, 0, 0, 0, 0}},
};

inline constexpr Site kSixtySites[] = {
    {0x00b6e67a, "bb01000000909090909090"},
    {0x00b6fbd4, "047df603"},
    {0x00eb9f11, "f3098a04"},
    {0x00fbc40f, "eb"},
    {0x01444947, "bd5f3104"},
    {0x0183dc97, "d605"},
    {0x0183dcfe, "00"},
    {0x0183ff29, "41c7859c010000cdcc4c3d41c785a0010000cdcc4c3d41c785a4010000cdcc4c3d41c785a8010000cdcc4c3d41c785b0010000cdcc4c3d41c785b4010000cdcc4c3de90b010000"},
    {0x01840799, "41c78538020000c2b8323f41c7853c0200000ad7233d41c785b80100008fc2753c41c785c4010000cdcc4c3e41c785c8010000cdcc4c3e41c785cc0100009a99193e41c785400200009a99193ee91e000000"},
    {0x01bf9d2a, "da0bb6"},
    {0x01c0f64b, "b9b2b4"},
    {0x01cbc4fb, "09e4a9"},
    {0x01cbc511, "f3e3a9"},
    {0x021bc184, "70437803488b3fc5fa59876402000090909090909090909090e96e0100"},
    {0x021bc308, "e974feffff909090"},
    {0x02377d4e, "b62b3e03"},
    {0x02418e3d, "f30f108364020000f30f5f4318f30f5d0512d59002f30f1145c8f30f1105a91a3403488d05aa7652039090909090"},
    {0x0267784f, "b5300e03"},
    {0x02677896, "6e300e03"},
    {0x02713db1, "536b0403"},
    {0x02713dbb, "496b0403"},
    {0x02715d75, "8f4b0403"},
    {0x03abc208, "01"},
    {0x03abc42d, "eb"},
    {0x03abc459, "14"},
    {0x03abc67e, "eb"},
    {0x03abc6aa, "c8"},
    {0x03abc8cf, "eb"},
    {0x03abc8fb, "c8"},
    {0x03abcb20, "eb"},
    {0x047df586, "803e"},
    {0x0575a908, "8988883c"},
};

inline constexpr Site kFrameTimeSites[] = {
    {0x016a4416, "c5fa5805ea640b04"},
    {0x01915fab, "c5fa10055549e403"},
    {0x01916030, "c5fa1005d048e403"},
    {0x01915f65, "488b7b20c5fa2ac9c5f2590def034103c5fa10058b49e40331c9f680e1010000040f94c14c89f64c89fa5890"},
    {0x0222bcbf, "488d0d7a404c0348898d40feffff8b0d35ec5203898d48feffff6690"},
    {0x01bf9b69, "8b05990db6038945b8660f1f440000"},
    {0x02713d8d, "c5fa1025736b0403"},
};

inline constexpr Site kClothSites[] = {
    {0x00b20dee, "e921000000"},
    {0x00b20e12, "eb17c5f85905e4ae2104c5f82945a0e9cdffffff9090909090"},
    {0x00b2b2fe, "e921000000"},
    {0x00b2b322, "eb17c5f85905d4092104c5f82945a0e9cdffffff9090909090"},
    {0x00b30ade, "e921000000"},
    {0x00b30b02, "eb17c5f85905f4b12004c5f82945a0e9cdffffff9090909090"},
    {0x00b38711, "e929000000"},
    {0x00b3873d, "eb17c5f85905b9352004c5f82945b0e9c5ffffff9090909090"},
    {0x00b41f4e, "e921000000"},
    {0x00b41f72, "eb17c5f85905849d1f04c5f8294580e9cdffffff9090909090"},
    {0x00b420ee, "e921000000"},
    {0x00b42112, "eb17c5f85905e49b1f04c5f82945a0e9cdffffff9090909090"},
};

inline constexpr DtLoad kDtLoads[] = {
    {0x01e4abca, 8, 4, 0x04d0b5b0, Kind::Keep, 4, 0.95f, "c5f85905de09ec02"},
    {0x01914c4d, 8, 4, 0x04d26334, Kind::Keep, 1, 0.8f, "c5fa5905df164103"},
    {0x01914c77, 8, 4, 0x04d26338, Kind::Keep, 1, 1.2f, "c5fa5905b9164103"},
    {0x01e46bd6, 8, 4, 0x05528374, Kind::Keep, 1, 0.65f, "c5fa100596176e03"},
    {0x01e46c52, 8, 4, 0x05528370, Kind::Amount, 1, 1.0f, "c5fa100516176e03"},
    {0x016dc848, 8, 4, 0x04d25940, Kind::Amount, 1, -0.0333333351f, "c5fa5805f0906403"},
    {0x01d3251e, 8, 4, 0x04d280ac, Kind::Amount, 1, 0.05f, "c5fa5805865bff02"},
    {0x01d3259b, 8, 4, 0x04d280c0, Kind::Amount, 1, -0.05f, "c5fa58051d5bff02"},
    {0x01c60cc3, 8, 4, 0x04d02f10, Kind::Rate, 4, 30.0f, "c5f8590545220a03"},
    {0x01c61281, 8, 4, 0x04d02f70, Kind::Rate, 4, 30.0f, "c5f85905e71c0a03"},
    {0x016ab623, 8, 4, 0x04d25840, Kind::Rate, 1, 30.0f, "c5fa590d15a26703"},
    {0x016ab6cf, 8, 4, 0x04d25840, Kind::Rate, 1, 30.0f, "c5f25e0d69a16703"},
    {0x01914c24, 8, 4, 0x04d2632c, Kind::Rate, 1, 30.0f, "c5fa590500174103"},
    {0x01923a12, 8, 4, 0x04d26440, Kind::Rate, 1, 30.0f, "c5fa5905262a4003"},
    {0x01925982, 8, 4, 0x04d26478, Kind::Rate, 1, 30.0f, "c5e2591dee0a4003"},
    {0x01c0f15e, 8, 4, 0x04d275e8, Kind::Rate, 1, 30.0f, "c5f2590d82841103"},
    {0x019dc1f7, 8, 4, 0x04d26c9c, Kind::Step, 1, 0.0333333351f, "c5fa10059daa3403"},
    {0x01b3456e, 8, 4, 0x04d270e4, Kind::Step, 1, 0.0333333351f, "c5fa10056e2b1f03"},
    {0x01d084f5, 8, 4, 0x04d27fd4, Kind::Amount, 1, -0.1f, "c5fa580dd7fa0103"},
    {0x01d08522, 8, 4, 0x04d27fd4, Kind::Amount, 1, -0.1f, "c5fa5805aafa0103"},
    {0x01d08518, 8, 4, 0x04d27fe0, Kind::Amount, 1, 0.1f, "c5fa580dc0fa0103"},
    {0x01d0854c, 8, 4, 0x04d27fe0, Kind::Amount, 1, 0.1f, "c5fa580d8cfa0103"},
    {0x025d6ae2, 8, 4, 0x04d2979c, Kind::Amount, 1, 0.0166666675f, "c5fa101db22c7502"},
    {0x0183af8c, 8, 4, 0x04d25e68, Kind::Decay, 1, 0.1f, "c5f2590dd4ae4e03"},
    {0x0183e2b2, 8, 4, 0x04d25e94, Kind::Decay, 1, 0.0333333351f, "c5fa1015da7b4e03"},
};

} // namespace BbFrameRate
