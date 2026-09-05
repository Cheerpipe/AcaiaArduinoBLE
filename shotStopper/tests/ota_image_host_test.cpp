#define SHOT_STOPPER_HOST_TEST

#include "../ShotStopperOta.h"
#include "../ShotStopperOtaImage.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using shotstopper::OtaImageHeaderResult;
using shotstopper::OtaImageTag;
using shotstopper::OtaImageTagScanner;
using shotstopper::OtaPendingVerifyAction;
using shotstopper::decideOtaPendingVerify;
using shotstopper::parseOtaImageTagBody;
using shotstopper::validateOtaImageHeader;

constexpr uint32_t kConfirmMinMs = 15000;
constexpr uint32_t kConfirmDeadlineMs = 180000;

int failures = 0;

#define CHECK(condition)                                              \
  do {                                                                \
    if (!(condition)) {                                               \
      std::cerr << __func__ << ":" << __LINE__                        \
                << ": check failed: " << #condition << "\n";          \
      ++failures;                                                     \
      return;                                                         \
    }                                                                 \
  } while (false)

// The literal is assembled at run time so this test file cannot itself be
// mistaken for a tagged image by a future grep-based check.
std::string tagPrefix() {
  char buffer[shotstopper::OTA_TAG_PREFIX_CAPACITY] = {};
  shotstopper::otaTagPrefix(buffer, sizeof(buffer));
  return std::string(buffer);
}

std::string makeTag(const std::string &arch, const std::string &version,
                    const std::string &packed) {
  return tagPrefix() + "arch=" + arch + "|ver=" + version + "|packed=" +
         packed + "|END";
}

std::vector<uint8_t> makeValidHeader() {
  std::vector<uint8_t> image(shotstopper::OTA_IMAGE_PREFIX_BYTES, 0);
  image[0] = shotstopper::OTA_ESP_IMAGE_MAGIC;
  image[12] = 0x09;  // ESP_CHIP_ID_ESP32S3, little endian
  image[13] = 0x00;
  const uint32_t magic = shotstopper::OTA_APP_DESC_MAGIC;
  std::memcpy(image.data() + shotstopper::OTA_APP_DESC_OFFSET, &magic,
              sizeof(magic));
  const char *name = shotstopper::OTA_EXPECTED_PROJECT_NAME;
  std::memcpy(image.data() + shotstopper::OTA_APP_DESC_PROJECT_NAME_OFFSET,
              name, std::strlen(name));
  return image;
}

void testHeaderAcceptsRealShape() {
  const std::vector<uint8_t> image = makeValidHeader();
  CHECK(validateOtaImageHeader(image.data(), image.size()) ==
        OtaImageHeaderResult::OK);
}

void testHeaderRejectsShortInput() {
  const std::vector<uint8_t> image = makeValidHeader();
  CHECK(validateOtaImageHeader(image.data(), image.size() - 1) ==
        OtaImageHeaderResult::TOO_SHORT);
  CHECK(validateOtaImageHeader(nullptr, 4096) ==
        OtaImageHeaderResult::TOO_SHORT);
}

void testHeaderRejectsWrongMagic() {
  std::vector<uint8_t> image = makeValidHeader();
  image[0] = 0xE8;
  CHECK(validateOtaImageHeader(image.data(), image.size()) ==
        OtaImageHeaderResult::BAD_MAGIC);
}

void testHeaderRejectsOtherChips() {
  // ESP32-S2 is 0x0002; an S2 build must never reach an S3 slot.
  std::vector<uint8_t> image = makeValidHeader();
  image[12] = 0x02;
  CHECK(validateOtaImageHeader(image.data(), image.size()) ==
        OtaImageHeaderResult::WRONG_CHIP);
}

void testHeaderRejectsMissingAppDescriptor() {
  std::vector<uint8_t> image = makeValidHeader();
  image[shotstopper::OTA_APP_DESC_OFFSET] ^= 0xFF;
  CHECK(validateOtaImageHeader(image.data(), image.size()) ==
        OtaImageHeaderResult::BAD_APP_DESC);
}

void testHeaderRejectsForeignProject() {
  std::vector<uint8_t> image = makeValidHeader();
  const char *other = "some-other-project";
  std::memset(image.data() + shotstopper::OTA_APP_DESC_PROJECT_NAME_OFFSET, 0,
              shotstopper::OTA_APP_DESC_NAME_BYTES);
  std::memcpy(image.data() + shotstopper::OTA_APP_DESC_PROJECT_NAME_OFFSET,
              other, std::strlen(other));
  CHECK(validateOtaImageHeader(image.data(), image.size()) ==
        OtaImageHeaderResult::WRONG_PROJECT);
}

void testHeaderAcceptsIdfProjectName() {
  std::vector<uint8_t> image = makeValidHeader();
  const char *idfName = shotstopper::OTA_EXPECTED_PROJECT_NAME_IDF;
  std::memset(image.data() + shotstopper::OTA_APP_DESC_PROJECT_NAME_OFFSET, 0,
              shotstopper::OTA_APP_DESC_NAME_BYTES);
  std::memcpy(image.data() + shotstopper::OTA_APP_DESC_PROJECT_NAME_OFFSET,
              idfName, std::strlen(idfName));
  CHECK(validateOtaImageHeader(image.data(), image.size()) ==
        OtaImageHeaderResult::OK);
}

void testTagBodyParsesEveryField() {
  const std::string tag = makeTag("n16r8", "1.2.3+abc1234", "16908291");
  const std::string body = tag.substr(tagPrefix().size());
  OtaImageTag parsed;
  CHECK(parseOtaImageTagBody(body.c_str(), body.size(), parsed));
  CHECK(parsed.valid);
  CHECK(std::string(parsed.arch) == "n16r8");
  CHECK(std::string(parsed.version) == "1.2.3+abc1234");
  CHECK(parsed.packed == 16908291U);
}

void testTagBodyRejectsMalformedInput() {
  OtaImageTag parsed;
  const std::string missingTerminator = "arch=n16r8|ver=1.0.0|packed=1";
  CHECK(!parseOtaImageTagBody(missingTerminator.c_str(),
                              missingTerminator.size(), parsed));

  const std::string missingVersion = "arch=n16r8|packed=1|END";
  CHECK(!parseOtaImageTagBody(missingVersion.c_str(), missingVersion.size(),
                              parsed));

  const std::string missingPacked = "arch=n16r8|ver=1.0.0|END";
  CHECK(!parseOtaImageTagBody(missingPacked.c_str(), missingPacked.size(),
                              parsed));

  const std::string badPacked = "arch=n16r8|ver=1.0.0|packed=12x|END";
  CHECK(!parseOtaImageTagBody(badPacked.c_str(), badPacked.size(), parsed));

  const std::string overflowPacked =
      "arch=n16r8|ver=1.0.0|packed=99999999999|END";
  CHECK(!parseOtaImageTagBody(overflowPacked.c_str(), overflowPacked.size(),
                              parsed));

  const std::string badArch = "arch=N16R8|ver=1.0.0|packed=1|END";
  CHECK(!parseOtaImageTagBody(badArch.c_str(), badArch.size(), parsed));

  const std::string emptyArch = "arch=|ver=1.0.0|packed=1|END";
  CHECK(!parseOtaImageTagBody(emptyArch.c_str(), emptyArch.size(), parsed));

  const std::string longArch =
      "arch=abcdefghijklmnopqrstuvwxyz|ver=1.0.0|packed=1|END";
  CHECK(!parseOtaImageTagBody(longArch.c_str(), longArch.size(), parsed));

  const std::string spaceInVersion = "arch=n16r8|ver=1.0 0|packed=1|END";
  CHECK(!parseOtaImageTagBody(spaceInVersion.c_str(), spaceInVersion.size(),
                              parsed));
}

void testArchUsability() {
  CHECK(shotstopper::otaArchIsUsable("n16r8"));
  CHECK(shotstopper::otaArchIsUsable("n8r4"));
  CHECK(!shotstopper::otaArchIsUsable("unknown"));
  CHECK(!shotstopper::otaArchIsUsable(""));
  CHECK(!shotstopper::otaArchIsUsable(nullptr));
  CHECK(!shotstopper::otaArchIsUsable("N16R8"));
}

std::vector<uint8_t> bytesOf(const std::string &text) {
  return std::vector<uint8_t>(text.begin(), text.end());
}

// Replays the same stream with every possible chunk boundary so a tag that
// straddles two TCP reads is still found.
bool scanEverySplit(const std::string &stream, OtaImageTag &tagOut,
                    uint32_t &offsetOut) {
  bool consistent = true;
  bool firstResult = false;
  OtaImageTag firstTag;
  uint32_t firstOffset = 0;
  for (size_t split = 0; split <= stream.size(); ++split) {
    OtaImageTagScanner scanner;
    const std::vector<uint8_t> head = bytesOf(stream.substr(0, split));
    const std::vector<uint8_t> tail = bytesOf(stream.substr(split));
    scanner.feed(head.data(), head.size());
    scanner.feed(tail.data(), tail.size());
    if (split == 0) {
      firstResult = scanner.found();
      firstTag = scanner.tag();
      firstOffset = scanner.tagOffset();
      continue;
    }
    if (scanner.found() != firstResult) {
      consistent = false;
      break;
    }
    if (firstResult &&
        (std::string(scanner.tag().arch) != std::string(firstTag.arch) ||
         std::string(scanner.tag().version) != std::string(firstTag.version) ||
         scanner.tag().packed != firstTag.packed ||
         scanner.tagOffset() != firstOffset)) {
      consistent = false;
      break;
    }
  }
  tagOut = firstTag;
  offsetOut = firstOffset;
  return consistent && firstResult;
}

void testScannerFindsTagAcrossEveryChunkBoundary() {
  const std::string stream =
      std::string("\xE9\x00 padding before ") +
      makeTag("n8r4", "2.0.1+deadbee", "33554433") + "\x00 trailing bytes";
  OtaImageTag tag;
  uint32_t offset = 0;
  CHECK(scanEverySplit(stream, tag, offset));
  CHECK(std::string(tag.arch) == "n8r4");
  CHECK(std::string(tag.version) == "2.0.1+deadbee");
  CHECK(tag.packed == 33554433U);
  CHECK(offset == stream.find(tagPrefix()));
}

void testScannerRecoversFromOverlappingPrefix() {
  // Every partial copy of the prefix immediately followed by the real tag.
  // "SHOT" is the case that matters: the mismatching byte is not the first
  // character of the prefix, so a scanner that restarts from scratch swallows
  // the byte that begins the genuine match and never finds the tag.
  const std::string prefix = tagPrefix();
  for (size_t overlapLength = 1; overlapLength < prefix.size();
       ++overlapLength) {
    const std::string overlap = prefix.substr(0, overlapLength);
    const std::string stream =
        overlap + makeTag("n16r8", "1.0.0+aaaaaaa", "16777216");
    OtaImageTag tag;
    uint32_t offset = 0;
    if (!scanEverySplit(stream, tag, offset) ||
        std::string(tag.arch) != "n16r8" || offset != overlap.size()) {
      std::cerr << __func__ << ": missed the tag after a " << overlapLength
                << "-character prefix fragment\n";
      ++failures;
      return;
    }
  }
}

void testScannerSkipsUnparseableCandidates() {
  // A bare prefix (as it appears inside the verifier's own code) must not
  // shadow the real tag further along the image.
  const std::string stream = tagPrefix() + std::string(1, '\0') +
                             "filler" +
                             makeTag("n16r8", "9.9.9+ffffffff", "151060479");
  OtaImageTag tag;
  uint32_t offset = 0;
  CHECK(scanEverySplit(stream, tag, offset));
  CHECK(std::string(tag.version) == "9.9.9+ffffffff");
  CHECK(tag.packed == 151060479U);
}

void testScannerSkipsMalformedTagBeforeValidOne() {
  const std::string stream =
      makeTag("n16r8", "1.0.0", "not-a-number") + "----" +
      makeTag("n8r4", "3.1.4+1234567", "50397188");
  OtaImageTag tag;
  uint32_t offset = 0;
  CHECK(scanEverySplit(stream, tag, offset));
  CHECK(std::string(tag.arch) == "n8r4");
  CHECK(tag.packed == 50397188U);
}

void testScannerReportsNothingWithoutTag() {
  const std::string stream(4096, 'x');
  OtaImageTagScanner scanner;
  const std::vector<uint8_t> bytes = bytesOf(stream);
  scanner.feed(bytes.data(), bytes.size());
  CHECK(!scanner.found());
  CHECK(!scanner.tag().valid);
}

void testScannerIgnoresOversizedCandidate() {
  // The previous test used filler of BODY_CAPACITY + 40, which is the one
  // alignment where the overflow reset landed exactly on the leading 'S' of
  // the next tag and hid the miss for every other length.
  const std::string valid = makeTag("n16r8", "1.0.0+abcdef0", "16777216");
  for (size_t filler = 0; filler <= shotstopper::OTA_TAG_BODY_CAPACITY + 8;
       ++filler) {
    const std::string stream = tagPrefix() + std::string(filler, 'z') + valid;
    OtaImageTag tag;
    uint32_t offset = 0;
    CHECK(scanEverySplit(stream, tag, offset));
    CHECK(std::string(tag.arch) == "n16r8");
    CHECK(tag.packed == 16777216U);
  }
}

void testScannerFindsTagAfterUnparseableCandidate() {
  const std::string valid = makeTag("n8r4", "2.0.0", "33554432");
  const std::string stream = tagPrefix() + "+Vna" + valid;
  OtaImageTag tag;
  uint32_t offset = 0;
  CHECK(scanEverySplit(stream, tag, offset));
  CHECK(std::string(tag.arch) == "n8r4");
}

void testScannerBoundsReplaysOnAdversarialCandidates() {
  // A long run of candidate bodies that each fail parse (bad packed field)
  // must not drive unbounded nested replay frames; the replay cap drops
  // captures instead, and a clean tag after the noise is still found.
  std::string stream;
  for (int i = 0; i < 32; ++i) {
    stream += makeTag("n16r8", "1.0.0", "not-a-number");
    stream += std::to_string(i);
  }
  stream += makeTag("n8r4", "3.1.4+1234567", "50397188");
  OtaImageTag tag;
  uint32_t offset = 0;
  CHECK(scanEverySplit(stream, tag, offset));
  CHECK(std::string(tag.arch) == "n8r4");
  CHECK(tag.packed == 50397188U);
}

void testPendingVerifyKeepRunningWhenRollbackImpossible() {
  CHECK(decideOtaPendingVerify(true, false, false, kConfirmDeadlineMs,
                               kConfirmMinMs, kConfirmDeadlineMs, false) ==
        OtaPendingVerifyAction::KEEP_RUNNING);
}

void testPendingVerifyRejectWhenRollbackPossible() {
  CHECK(decideOtaPendingVerify(true, false, false, kConfirmDeadlineMs,
                               kConfirmMinMs, kConfirmDeadlineMs, true) ==
        OtaPendingVerifyAction::REJECT);
}

void testPendingVerifyConfirmAfterHttpUptime() {
  CHECK(decideOtaPendingVerify(true, false, true, kConfirmMinMs, kConfirmMinMs,
                               kConfirmDeadlineMs, true) ==
        OtaPendingVerifyAction::CONFIRM);
}

void testPendingVerifyWaitsWithoutHttp() {
  CHECK(decideOtaPendingVerify(true, false, false, kConfirmMinMs, kConfirmMinMs,
                               kConfirmDeadlineMs, true) ==
        OtaPendingVerifyAction::WAIT);
}

void testPendingVerifyWaitsBeforeConfirmUptime() {
  CHECK(decideOtaPendingVerify(true, false, true, kConfirmMinMs - 1,
                               kConfirmMinMs, kConfirmDeadlineMs, true) ==
        OtaPendingVerifyAction::WAIT);
}

void testPendingVerifyNoneWhenSettledOrNotPending() {
  CHECK(decideOtaPendingVerify(true, true, true, kConfirmMinMs, kConfirmMinMs,
                               kConfirmDeadlineMs, true) ==
        OtaPendingVerifyAction::NONE);
  CHECK(decideOtaPendingVerify(false, false, true, kConfirmMinMs, kConfirmMinMs,
                               kConfirmDeadlineMs, true) ==
        OtaPendingVerifyAction::NONE);
}

void testPendingVerifyHttpReadyAfterDeadlineStillConfirms() {
  CHECK(decideOtaPendingVerify(true, false, true, kConfirmDeadlineMs,
                               kConfirmMinMs, kConfirmDeadlineMs, false) ==
        OtaPendingVerifyAction::CONFIRM);
}

void testPendingVerifyDefersConfirmWhileFlashUnsafe() {
  CHECK(decideOtaPendingVerify(true, false, true, kConfirmMinMs, kConfirmMinMs,
                               kConfirmDeadlineMs, true, false) ==
        OtaPendingVerifyAction::WAIT);
}

void testPendingVerifyConfirmsAtDeadlineEvenIfFlashUnsafe() {
  CHECK(decideOtaPendingVerify(true, false, true, kConfirmDeadlineMs,
                               kConfirmMinMs, kConfirmDeadlineMs, true, false) ==
        OtaPendingVerifyAction::CONFIRM);
}

}  // namespace

int main() {
  testHeaderAcceptsRealShape();
  testHeaderRejectsShortInput();
  testHeaderRejectsWrongMagic();
  testHeaderRejectsOtherChips();
  testHeaderRejectsMissingAppDescriptor();
  testHeaderRejectsForeignProject();
  testHeaderAcceptsIdfProjectName();
  testTagBodyParsesEveryField();
  testTagBodyRejectsMalformedInput();
  testArchUsability();
  testScannerFindsTagAcrossEveryChunkBoundary();
  testScannerRecoversFromOverlappingPrefix();
  testScannerSkipsUnparseableCandidates();
  testScannerSkipsMalformedTagBeforeValidOne();
  testScannerReportsNothingWithoutTag();
  testScannerIgnoresOversizedCandidate();
  testScannerFindsTagAfterUnparseableCandidate();
  testScannerBoundsReplaysOnAdversarialCandidates();
  testPendingVerifyKeepRunningWhenRollbackImpossible();
  testPendingVerifyRejectWhenRollbackPossible();
  testPendingVerifyConfirmAfterHttpUptime();
  testPendingVerifyWaitsWithoutHttp();
  testPendingVerifyWaitsBeforeConfirmUptime();
  testPendingVerifyNoneWhenSettledOrNotPending();
  testPendingVerifyHttpReadyAfterDeadlineStillConfirms();
  testPendingVerifyDefersConfirmWhileFlashUnsafe();
  testPendingVerifyConfirmsAtDeadlineEvenIfFlashUnsafe();

  if (failures != 0) {
    std::cerr << "ota image host test failures: " << failures << "\n";
    return EXIT_FAILURE;
  }
  std::cout << "OTA image parser: header, tag and chunk-boundary scanning OK\n";
  return EXIT_SUCCESS;
}
