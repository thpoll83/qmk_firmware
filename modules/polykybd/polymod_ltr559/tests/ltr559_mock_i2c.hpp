// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// A mock LTR-559 sitting on the QMK i2c_master bus, so the unit tests exercise
// the REAL driver end to end (probe -> config -> poll -> decode) rather than a
// re-implementation of its arithmetic. The mock models the part as a 256-byte
// auto-incrementing register file, which is what the driver actually talks to:
// it reads 4 bytes from 0x88 and 2 from 0x8D and relies on that auto-increment,
// so a per-register mock would not catch an endianness or offset mistake.

#pragma once

#include <cstdint>
#include <vector>

class LtrMockI2C {
   public:
    static LtrMockI2C& Instance();

    // Clears every register, counter and injected fault, and puts a healthy
    // LTR-559 on the bus. Call at the top of each test.
    void reset();

    // --- bus presence / injected faults ---------------------------------

    // false = nothing ACKs at the LTR-559 address (the "no sensor fitted" case).
    void set_present(bool present) {
        present_ = present;
    }
    // Make every read fail, e.g. to simulate a transient bus error mid-poll.
    void set_reads_fail(bool fail) {
        reads_fail_ = fail;
    }
    // Make a write to this register fail (0x00 = none). Used to prove the driver
    // refuses to mark the part present when configuration did not land.
    void set_failing_write_reg(uint8_t reg) {
        failing_write_reg_ = reg;
    }

    // --- device state the driver reads ----------------------------------

    void set_ids(uint8_t part_id, uint8_t manufac_id);
    // status = the ALS_PS_STATUS byte (data-new / valid bits).
    void set_status(uint8_t status) {
        regs_[0x8C] = status;
    }
    // Writes the two ALS channels in the part's own byte order at 0x88.
    void set_als(uint16_t ch0, uint16_t ch1);
    // Writes the 11-bit proximity + saturation flag at 0x8D.
    void set_prox(uint16_t prox, bool saturated);

    // --- observations ----------------------------------------------------

    uint8_t reg(uint8_t r) const {
        return regs_[r];
    }
    // Every register the driver wrote, in order, as (reg, value) pairs.
    const std::vector<std::pair<uint8_t, uint8_t>>& writes() const {
        return writes_;
    }
    uint32_t i2c_init_calls() const {
        return i2c_init_calls_;
    }
    // How many times the driver read PART_ID, i.e. how many probes it attempted.
    uint32_t probe_attempts() const {
        return probe_attempts_;
    }
    // How many times the driver read ALS_PS_STATUS, i.e. how many polls ran.
    uint32_t status_reads() const {
        return status_reads_;
    }

    // --- called by the extern "C" i2c_master shims -----------------------
    int16_t do_read(uint8_t devaddr, uint8_t regaddr, uint8_t* data, uint16_t length);
    int16_t do_write(uint8_t devaddr, uint8_t regaddr, const uint8_t* data, uint16_t length);
    void    note_i2c_init() {
        ++i2c_init_calls_;
    }

   private:
    LtrMockI2C() {
        reset();
    }

    uint8_t                                  regs_[256];
    bool                                     present_           = true;
    bool                                     reads_fail_        = false;
    uint8_t                                  failing_write_reg_ = 0;
    std::vector<std::pair<uint8_t, uint8_t>> writes_;
    uint32_t                                 i2c_init_calls_ = 0;
    uint32_t                                 probe_attempts_ = 0;
    uint32_t                                 status_reads_   = 0;
};
