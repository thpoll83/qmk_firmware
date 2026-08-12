// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ltr559_mock_i2c.hpp"

#include <cstring>

extern "C" {
#include "i2c_master.h"
}

// The driver talks to LTR559_I2C_ADDR (0x23) pre-shifted, as the QMK API wants.
static constexpr uint8_t kDevAddr8 = 0x23 << 1;

LtrMockI2C& LtrMockI2C::Instance() {
    static LtrMockI2C instance;
    return instance;
}

void LtrMockI2C::reset() {
    std::memset(regs_, 0, sizeof(regs_));
    present_           = true;
    reads_fail_        = false;
    failing_write_reg_ = 0;
    writes_.clear();
    i2c_init_calls_ = 0;
    probe_attempts_ = 0;
    status_reads_   = 0;
    set_ids(0x92, 0x05); // the real part's PART_ID / MANUFAC_ID
}

void LtrMockI2C::set_ids(uint8_t part_id, uint8_t manufac_id) {
    regs_[0x86] = part_id;
    regs_[0x87] = manufac_id;
}

void LtrMockI2C::set_als(uint16_t ch0, uint16_t ch1) {
    // 0x88..0x8B = CH1_0, CH1_1, CH0_0, CH0_1 — little-endian *per channel*, and
    // CH1 comes FIRST. Getting this pair the wrong way round is the mistake the
    // decode test exists to catch, so the mock lays it out the way the part does.
    regs_[0x88] = (uint8_t)(ch1 & 0xFF);
    regs_[0x89] = (uint8_t)(ch1 >> 8);
    regs_[0x8A] = (uint8_t)(ch0 & 0xFF);
    regs_[0x8B] = (uint8_t)(ch0 >> 8);
}

void LtrMockI2C::set_prox(uint16_t prox, bool saturated) {
    regs_[0x8D] = (uint8_t)(prox & 0xFF);
    regs_[0x8E] = (uint8_t)((prox >> 8) & 0x07) | (saturated ? 0x80 : 0x00);
}

int16_t LtrMockI2C::do_read(uint8_t devaddr, uint8_t regaddr, uint8_t* data, uint16_t length) {
    if (regaddr == 0x86) {
        ++probe_attempts_;
    }
    if (regaddr == 0x8C) {
        ++status_reads_;
    }
    if (devaddr != kDevAddr8 || !present_ || reads_fail_) {
        return I2C_STATUS_TIMEOUT;
    }
    // Auto-incrementing register file, as the real part does for the ALS/PS
    // multi-byte reads.
    for (uint16_t i = 0; i < length; ++i) {
        data[i] = regs_[(uint8_t)(regaddr + i)];
    }
    return I2C_STATUS_SUCCESS;
}

int16_t LtrMockI2C::do_write(uint8_t devaddr, uint8_t regaddr, const uint8_t* data, uint16_t length) {
    if (devaddr != kDevAddr8 || !present_) {
        return I2C_STATUS_TIMEOUT;
    }
    if (failing_write_reg_ != 0 && regaddr == failing_write_reg_) {
        return I2C_STATUS_TIMEOUT;
    }
    for (uint16_t i = 0; i < length; ++i) {
        regs_[(uint8_t)(regaddr + i)] = data[i];
        writes_.emplace_back((uint8_t)(regaddr + i), data[i]);
    }
    return I2C_STATUS_SUCCESS;
}

// ── the i2c_master surface the driver links against ─────────────────────────

extern "C" {

void i2c_init(void) {
    LtrMockI2C::Instance().note_i2c_init();
}

i2c_status_t i2c_read_register(uint8_t devaddr, uint8_t regaddr, uint8_t* data, uint16_t length, uint16_t timeout) {
    (void)timeout;
    return LtrMockI2C::Instance().do_read(devaddr, regaddr, data, length);
}

i2c_status_t i2c_write_register(uint8_t devaddr, uint8_t regaddr, const uint8_t* data, uint16_t length, uint16_t timeout) {
    (void)timeout;
    return LtrMockI2C::Instance().do_write(devaddr, regaddr, data, length);
}

} // extern "C"
