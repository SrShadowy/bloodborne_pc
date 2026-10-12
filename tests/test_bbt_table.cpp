// SPDX-License-Identifier: GPL-2.0-or-later
#include "mem_editor.h"
#include <cassert>
#include <cstdio>
#include <unistd.h>

int main() {
    auto& editor = Debugger::MemoryEditor::Get();
    editor.Clear();

    // 1. Setup multi-level pointer in memory:
    // Target Value: 123456789
    // Level 1 ptr + 0x94 = Target
    // Base ptr + 0x08 = Level 1 ptr
    uint32_t target_val = 150;
    uintptr_t target_addr = reinterpret_cast<uintptr_t>(&target_val);

    uintptr_t level1_ptr = target_addr - 0x94;
    uintptr_t level1_ptr_addr = reinterpret_cast<uintptr_t>(&level1_ptr);

    uintptr_t base_ptr = level1_ptr_addr - 0x08;
    uintptr_t base_ptr_addr = reinterpret_cast<uintptr_t>(&base_ptr);

    // 2. Add Pointer Watch with offsets { 0x08, 0x94 }
    editor.AddPointerWatch("Blood Echoes", base_ptr_addr, { 0x08, 0x94 }, Debugger::DataType::U32);
    assert(editor.GetCount() == 1);

    auto entries = editor.GetEntries();
    assert(entries[0].label == "Blood Echoes");
    assert(entries[0].is_pointer == true);
    assert(entries[0].offsets.size() == 2);
    assert(entries[0].offsets[0] == 0x08);
    assert(entries[0].offsets[1] == 0x94);

    // 3. Test Resolution
    uintptr_t resolved = editor.ResolveAddress(entries[0]);
    assert(resolved == target_addr);

    // 4. Test Write via Pointer
    bool ok_write = editor.WriteValue(0, 999999);
    assert(ok_write);
    assert(target_val == 999999);

    // 5. Test Null Pointer Safety
    uintptr_t null_base = 0;
    uintptr_t null_base_addr = reinterpret_cast<uintptr_t>(&null_base);
    editor.AddPointerWatch("Null Ptr Test", null_base_addr, { 0x10, 0x20 }, Debugger::DataType::U32);
    entries = editor.GetEntries();
    uintptr_t null_resolved = editor.ResolveAddress(entries[1]);
    assert(null_resolved == 0); // Must be safely 0

    // 6. Test BBT (Bloodborne Table) Serialization & Deserialization
    const char* test_path = "/tmp/unit_test_table.bbt";
    unlink(test_path);
    bool saved = editor.SaveTableBBT(test_path);
    assert(saved);

    editor.Clear();
    assert(editor.GetCount() == 0);

    bool loaded = editor.LoadTableBBT(test_path);
    assert(loaded);
    assert(editor.GetCount() == 2);

    auto loaded_entries = editor.GetEntries();
    assert(loaded_entries[0].label == "Blood Echoes");
    assert(loaded_entries[0].is_pointer == true);
    assert(loaded_entries[0].offsets.size() == 2);
    assert(loaded_entries[0].offsets[0] == 0x08);
    assert(loaded_entries[0].offsets[1] == 0x94);

    uintptr_t reload_resolved = editor.ResolveAddress(loaded_entries[0]);
    assert(reload_resolved == target_addr);

    unlink(test_path);
    std::puts("PASS: bbt-table-test passed all multi-level pointer and serialization checks!");
    return 0;
}
