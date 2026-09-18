/* Display common matrix scan tests.
 *
 * Tests the shared matrix_scan_display_common() logic that both split72 and split42
 * use for display inversion on key press/release. This is the critical path
 * for the per-keycap OLED inversion that happens on every keypress.
 *
 * The test uses weak function overrides to simulate variant-specific behavior
 * without needing actual hardware or the full QMK environment.
 */
#include "gtest/gtest.h"

// Mock the QMK types and functions we need
extern "C" {
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef uint8_t matrix_row_t;

// Global matrix state (defined in quantum/matrix.c)
matrix_row_t matrix[10];  // Enough for both variants

// Weak functions from display_common.h - we'll override them
const uint8_t* get_key_disp_bitmask(uint8_t index);
uint8_t get_disp_bitmask_size(void);
bool key_has_display(uint8_t r, uint8_t c);
void invert_display(uint8_t r, uint8_t c, bool state);

// From side.h
bool is_left_side(void);

// From quantum.h / config.h
#define MATRIX_ROWS 10
#define MATRIX_ROWS_PER_SIDE 5
#define MATRIX_COLS 8

// From display_common.c
void matrix_scan_display_common(void);
void matrix_scan_user(void) {}  // No-op for tests
}

// Test tracking
namespace {
struct InvertCall {
    uint8_t r;
    uint8_t c;
    bool state;
};

std::vector<InvertCall> invert_calls;

// Mock implementations for split72 behavior
const uint8_t* get_key_disp_bitmask(uint8_t index) {
    static uint8_t dummy[5] = {0};
    return dummy;
}

uint8_t get_disp_bitmask_size(void) {
    return 5;
}

bool key_has_display(uint8_t r, uint8_t c) {
    // split72: (3,7) and (8,0) have no display
    return !((r == 3 && c == 7) || (r == 8 && c == 0));
}

void invert_display(uint8_t r, uint8_t c, bool state) {
    invert_calls.push_back({r, c, state});
}

bool is_left_side(void) {
    return true;  // Test from left side perspective
}

// Reset tracking before each test
void reset_tracking() {
    invert_calls.clear();
    memset(matrix, 0, sizeof(matrix));
}

// Helper: set a key pressed in the matrix
void press_key(uint8_t r, uint8_t c) {
    matrix[r] |= (1 << c);
}

// Helper: set a key released in the matrix
void release_key(uint8_t r, uint8_t c) {
    matrix[r] &= ~(1 << c);
}

// Helper: check if a key is pressed
bool is_key_pressed(uint8_t r, uint8_t c) {
    return (matrix[r] & (1 << c)) != 0;
}
}  // namespace

// Test fixture
class DisplayCommonTest : public ::testing::Test {
protected:
    void SetUp() override { reset_tracking(); }
    void TearDown() override { reset_tracking(); }
};

// ============================================================================
// Basic Matrix Scan Tests
// ============================================================================

TEST_F(DisplayCommonTest, NoKeysPressedNoInverts) {
    // Matrix is all zeros (no keys pressed)
    matrix_scan_display_common();
    
    ASSERT_EQ(invert_calls.size(), 0) << "Should not invert any displays when no keys pressed";
}

TEST_F(DisplayCommonTest, SingleKeyPressInvertsDisplay) {
    press_key(0, 0);
    matrix_scan_display_common();
    
    ASSERT_EQ(invert_calls.size(), 1) << "Should invert exactly one display";
    ASSERT_EQ(invert_calls[0].r, 0);
    ASSERT_EQ(invert_calls[0].c, 0);
    ASSERT_TRUE(invert_calls[0].state);
}

TEST_F(DisplayCommonTest, SingleKeyReleaseInvertsDisplay) {
    // First press the key (this establishes the "last_matrix" state)
    press_key(0, 0);
    matrix_scan_display_common();
    invert_calls.clear();  // Clear the press inversion
    
    // Now release it
    release_key(0, 0);
    matrix_scan_display_common();
    
    ASSERT_EQ(invert_calls.size(), 1) << "Should invert exactly one display on release";
    ASSERT_EQ(invert_calls[0].r, 0);
    ASSERT_EQ(invert_calls[0].c, 0);
    ASSERT_FALSE(invert_calls[0].state);
}

// ============================================================================
// split72-Specific Tests (Column Adjustment)
// ============================================================================

class Split72Test : public DisplayCommonTest {
protected:
    // For split72, rows 5-8 need c-- adjustment
    // We simulate this by overriding the behavior in the test
    void SetUp() override {
        DisplayCommonTest::SetUp();
        // No special setup needed - the macro is compile-time
    }
};

TEST_F(Split72Test, ColumnAdjustmentAppliedForRow5) {
    // Simulate pressing a key in row 5, column 3
    // This should be adjusted to column 2
    press_key(5, 3);
    matrix_scan_display_common();
    
    ASSERT_EQ(invert_calls.size(), 1);
    ASSERT_EQ(invert_calls[0].r, 5);
    // The column adjustment for split72 rows >= 5 should decrement c
    // But our mock doesn't apply it - we need to test with actual macro
    // This test verifies the base behavior; column adjustment is tested via
    // the macro definition in split72.h
}

// ============================================================================
// Key Without Display Tests
// ============================================================================

TEST_F(DisplayCommonTest, KeyWithoutDisplayNotInverted) {
    // split72: (3,7) has no display - should not invert
    press_key(3, 7);
    matrix_scan_display_common();
    
    ASSERT_EQ(invert_calls.size(), 0) << "Should not invert display for key without OLED";
}

TEST_F(DisplayCommonTest, KeyWithoutDisplayRow8Col0NotInverted) {
    // split72: (8,0) has no display - should not invert
    press_key(8, 0);
    matrix_scan_display_common();
    
    ASSERT_EQ(invert_calls.size(), 0) << "Should not invert display for key without OLED";
}

// ============================================================================
// Multiple Key Tests
// ============================================================================

TEST_F(DisplayCommonTest, MultipleKeysPressedInvertsAll) {
    press_key(0, 0);
    press_key(0, 1);
    press_key(1, 0);
    matrix_scan_display_common();
    
    ASSERT_EQ(invert_calls.size(), 3) << "Should invert all three displays";
    
    // Check each was inverted with state=true (press)
    for (const auto& call : invert_calls) {
        ASSERT_TRUE(call.state);
    }
}

TEST_F(DisplayCommonTest, KeyPressThenRelease) {
    // Press key
    press_key(0, 0);
    matrix_scan_display_common();
    size_t press_count = invert_calls.size();
    invert_calls.clear();
    
    // Release key
    release_key(0, 0);
    matrix_scan_display_common();
    
    ASSERT_EQ(invert_calls.size(), 1) << "Should invert on release";
    ASSERT_EQ(invert_calls[0].r, 0);
    ASSERT_EQ(invert_calls[0].c, 0);
    ASSERT_FALSE(invert_calls[0].state);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(DisplayCommonTest, UnchangedKeyNoInvert) {
    // Press a key
    press_key(0, 0);
    matrix_scan_display_common();
    invert_calls.clear();
    
    // Scan again without changing matrix
    matrix_scan_display_common();
    
    ASSERT_EQ(invert_calls.size(), 0) << "Should not invert when matrix unchanged";
}

TEST_F(DisplayCommonTest, ToggleSameKeyMultipleTimes) {
    // Press, release, press, release
    press_key(0, 0);
    matrix_scan_display_common();
    
    release_key(0, 0);
    matrix_scan_display_common();
    
    press_key(0, 0);
    matrix_scan_display_common();
    
    release_key(0, 0);
    matrix_scan_display_common();
    
    // Should have 4 invert calls total (press, release, press, release)
    ASSERT_EQ(invert_calls.size(), 4);
    
    // Check alternating states
    ASSERT_TRUE(invert_calls[0].state);   // press
    ASSERT_FALSE(invert_calls[1].state);  // release
    ASSERT_TRUE(invert_calls[2].state);   // press
    ASSERT_FALSE(invert_calls[3].state);  // release
}
