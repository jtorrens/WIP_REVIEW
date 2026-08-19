#include "color_pipeline.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>

using wipreview::color::DisplayConfig;
using wipreview::color::DisplayEncoding;
using wipreview::color::RGB;

namespace {

bool near(float actual, double expected, double tolerance = 1.0e-5) {
  return std::abs(static_cast<double>(actual) - expected) <= tolerance;
}

void assertRoundTrip(DisplayConfig config, RGB encoded, double tolerance) {
  const RGB linear = wipreview::color::decodeDisplay(encoded, config);
  const RGB roundTrip = wipreview::color::encodeDisplay(linear, config);
  for (std::size_t channel = 0; channel < encoded.size(); ++channel) {
    assert(near(roundTrip[channel], encoded[channel], tolerance));
  }
}

void testAutomaticGraphicsWhite() {
  assert(wipreview::color::automaticGraphicsWhiteNits(
             DisplayEncoding::Rec709Gamma24) == 100.0);
  assert(wipreview::color::automaticGraphicsWhiteNits(
             DisplayEncoding::Rec2100PQ) == 203.0);
  assert(wipreview::color::automaticGraphicsWhiteNits(
             DisplayEncoding::Rec2100HLG, 1000.0) == 203.0);
  assert(wipreview::color::automaticGraphicsWhiteNits(
             DisplayEncoding::Rec2100HLG, 2000.0) == 406.0);
}

void testRec709Gamma24() {
  DisplayConfig config;
  config.encoding = DisplayEncoding::Rec709Gamma24;
  const RGB linear = wipreview::color::decodeDisplay({0.5F, 1.0F, 0.0F}, config);
  assert(near(linear[0], 0.1894645708));
  assert(near(linear[1], 1.0));
  assert(near(linear[2], 0.0));

  const RGB halfWhite = wipreview::color::encodeDisplay({0.5F, 0.5F, 0.5F}, config);
  assert(near(halfWhite[0], 0.7491535384));
  assert(halfWhite[0] > 0.5F);  // Linear blend, not a blend in gamma-encoded values.
  assertRoundTrip(config, {0.02F, 0.5F, 1.0F}, 1.0e-6);
}

void testPqGraphicsWhiteAndRoundTrip() {
  DisplayConfig config;
  config.encoding = DisplayEncoding::Rec2100PQ;
  config.graphicsWhiteNits = 203.0;
  const RGB encodedWhite = wipreview::color::encodeDisplay({1.0F, 1.0F, 1.0F}, config);
  assert(near(encodedWhite[0], 0.5806888810, 2.0e-6));
  const RGB decodedWhite = wipreview::color::decodeDisplay(encodedWhite, config);
  assert(near(decodedWhite[0], 1.0, 2.0e-5));

  const float peakRelative = static_cast<float>(10000.0 / config.graphicsWhiteNits);
  const RGB encodedPeak = wipreview::color::encodeDisplay(
      {peakRelative, peakRelative, peakRelative}, config);
  assert(near(encodedPeak[0], 1.0, 2.0e-6));
  assertRoundTrip(config, {0.10F, 0.58F, 0.90F}, 2.0e-5);
}

void testHlgDisplayEotfAndRoundTrip() {
  DisplayConfig config;
  config.encoding = DisplayEncoding::Rec2100HLG;
  config.graphicsWhiteNits = 203.0;
  config.peakNits = 1000.0;

  const RGB referenceWhite = wipreview::color::decodeDisplay(
      {0.75F, 0.75F, 0.75F}, config);
  assert(near(referenceWhite[0], 1.0, 0.015));
  assertRoundTrip(config, {0.10F, 0.50F, 0.90F}, 2.0e-5);
  assertRoundTrip(config, {0.75F, 0.75F, 0.75F}, 2.0e-5);
}

void testGraphicsPickerIsRelativeToWhite() {
  const RGB picker{1.0F, 0.5F, 0.0F};
  assert(wipreview::color::graphicsColourToDisplayLinear(picker) == picker);
}

}  // namespace

int main() {
  testAutomaticGraphicsWhite();
  testRec709Gamma24();
  testPqGraphicsWhiteAndRoundTrip();
  testHlgDisplayEotfAndRoundTrip();
  testGraphicsPickerIsRelativeToWhite();
  return 0;
}
