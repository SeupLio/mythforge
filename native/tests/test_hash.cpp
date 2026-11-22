#include "mf_test.h"

#include "mfc/hash.h"

MF_TEST(sha256_matches_known_vectors) {
  MF_CHECK_STR_EQ(mfc::sha256Hex(""),
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  MF_CHECK_STR_EQ(mfc::sha256Hex("abc"),
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  MF_CHECK_STR_EQ(
      mfc::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
      "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

MF_TEST(sha256_handles_padding_boundaries) {
  // 55 and 56 bytes straddle the length-padding boundary of a single block.
  const std::string a(55, 'a');
  const std::string b(56, 'a');
  MF_CHECK_EQ(mfc::sha256Hex(a).size(), static_cast<size_t>(64));
  MF_CHECK_NE(mfc::sha256Hex(a), mfc::sha256Hex(b));
}

MF_TEST(fnv1a_is_deterministic) {
  MF_CHECK_EQ(mfc::fnv1a64("hello"), mfc::fnv1a64("hello"));
  MF_CHECK_NE(mfc::fnv1a64("hello"), mfc::fnv1a64("hellp"));
}
