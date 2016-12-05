/**
 * Simple test case for UTF-8 conversions.
 */

#include "common.h"
#include <gammu.h>
#include <gammu-unicode.h>
#include <string.h>

#include "../libgammu/misc/coding/coding.h"

int main(int argc UNUSED, char **argv UNUSED)
{
    unsigned char out[20];
    gammu_char_t dest;

    test_result(EncodeWithUTF8Alphabet(0x24, out) == 1);
    test_result(out[0] == 0x24);

    test_result(EncodeWithUTF8Alphabet(0xa2, out) == 2);
    test_result(out[0] == 0xc2);
    test_result(out[1] == 0xa2);

    test_result(EncodeWithUTF8Alphabet(0x20ac, out) == 3);
    test_result(out[0] == 0xe2);
    test_result(out[1] == 0x82);
    test_result(out[2] == 0xac);

    test_result(EncodeWithUTF8Alphabet(0x10348, out) == 4);
    test_result(out[0] == 0xf0);
    test_result(out[1] == 0x90);
    test_result(out[2] == 0x8d);
    test_result(out[3] == 0x88);

    test_result(EncodeWithUTF8Alphabet(0x1F44D, out) == 4);
    test_result(out[0] == 0xf0);
    test_result(out[1] == 0x9f);
    test_result(out[2] == 0x91);
    test_result(out[3] == 0x8d);

    test_result(EncodeUTF8(out, "\xD8\x3d\xDC\x4d\x00\x00"));

    test_result(out[0] == 0xf0);
    test_result(out[1] == 0x9f);
    test_result(out[2] == 0x91);
    test_result(out[3] == 0x8d);
    test_result(out[4] == 0x00);

    test_result(DecodeWithUTF8Alphabet("\xf0\x9f\x91\x8d\x00", &dest, 4) == 4);
    test_result(dest == 0x1f44d);

    DecodeUTF8(out, "\xf0\x9f\x91\x8d\x00", 4);

    test_string("\xD8\x3d\xDC\x4d\x00", out, 5);

    DecodeUTF8(out, "ahoj", 4);

    test_string("\x00\x61\x00h\x00o\x00j\x00\x00\x00", out, 10);

	return 0;
}

