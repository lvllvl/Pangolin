/* This file is part of the Pangolin Project.
 * http://github.com/stevenlovegrove/Pangolin
 *
 * Copyright (c) 2014 Steven Lovegrove
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <pangolin/factory/factory_registry.h>
#include <pangolin/video/drivers/transform.h>
#include <pangolin/video/iostream_operators.h>
#include <pangolin/video/video.h>

#include <unordered_map>
#include <vector>

namespace pangolin
{

void PitchedImageCopy(
    MutImageView<unsigned char>& img_out,
    const ImageView<unsigned char>& img_in, size_t bytes_per_pixel)
{
  if (img_out.imageSize() != img_in.imageSize()) {
    throw std::runtime_error("PitchedImageCopy: Incompatible image sizes");
  }

  for (int y = 0; y < img_out.height(); ++y) {
    std::memcpy(
        img_out.rowPtrMut((int)y), img_in.rowPtr((int)y),
        bytes_per_pixel * img_in.width());
  }
}

TransformVideo::TransformVideo(
    std::unique_ptr<VideoInterface>& src,
    const std::vector<TransformOptions>& flips) :
    videoin(std::move(src)), flips(flips), size_bytes(0), buffer(0)
{
  if (!videoin) {
    throw VideoException("TransformVideo: VideoInterface in must not be null");
  }

  inputs.push_back(videoin.get());

  for (size_t i = 0; i < videoin->Streams().size(); i++) switch (flips[i]) {
      case TransformOptions::FlipX:
      case TransformOptions::FlipY:
      case TransformOptions::FlipXY:
      case TransformOptions::None:
        streams.push_back(videoin->Streams()[i]);
        break;

      case TransformOptions::Transpose:
      case TransformOptions::RotateCW:
      case TransformOptions::RotateCCW:

        auto orig_shape = videoin->Streams()[i].shape();
        auto offset = videoin->Streams()[i].offsetBytes();
        auto fmt = videoin->Streams()[i].format();

        streams.emplace_back(
            fmt,
            ImageShape{
                orig_shape.height(), orig_shape.width(),
                orig_shape.pitchBytes()},
            offset);
        break;
    };

  size_bytes = videoin->SizeBytes();
  buffer = new uint8_t[size_bytes];
}

TransformVideo::~TransformVideo() { delete[] buffer; }

//! Implement VideoInput::Start()
void TransformVideo::Start() { videoin->Start(); }

//! Implement VideoInput::Stop()
void TransformVideo::Stop() { videoin->Stop(); }

//! Implement VideoInput::SizeBytes()
size_t TransformVideo::SizeBytes() const { return size_bytes; }

//! Implement VideoInput::Streams()
const std::vector<StreamInfo>& TransformVideo::Streams() const
{
  return streams;
}

void FlipY(
    MutImageView<uint8_t>& img_out, const ImageView<uint8_t>& img_in,
    size_t bytes_per_pixel)
{
  if (img_out.imageSize() != img_in.imageSize()) {
    throw std::runtime_error("FlipY: Incompatible image sizes");
  }

  for (int y_out = 0; y_out < img_out.height(); ++y_out) {
    const int y_in = (img_in.height() - 1) - y_out;
    std::memcpy(
        img_out.rowPtrMut((int)y_out), img_in.rowPtr((int)y_in),
        bytes_per_pixel * img_in.width());
  }
}

template <typename T>
void ChainSwap2(T& a, T& b)
{
  T t = a;
  a = b;
  b = t;
}

template <typename T>
void ChainSwap4(T& a, T& b, T& c, T& d)
{
  T t = a;
  a = b;
  b = c;
  c = d;
  d = t;
}

template <size_t BPP, size_t TSZ>
void TiledFlipX(
    MutImageView<uint8_t>& img_out, const ImageView<uint8_t>& img_in)
{
  const size_t w = img_in.width();
  const size_t h = img_in.height();

  typedef struct {
    uint8_t d[BPP];
  } T;
  T d[TSZ][TSZ];

  for (size_t xin = 0; xin < w; xin += TSZ)
    for (size_t yin = 0; yin < h; yin += TSZ) {
      const size_t xspan = std::min(TSZ, w - xin);
      const size_t yspan = std::min(TSZ, h - yin);
      const size_t xout = w - xin - TSZ;
      const size_t yout = yin;

      for (size_t y = 0; y < yspan; y++)
        memcpy(d[y], img_in.rowPtr(yin + y) + xin * BPP, xspan * BPP);

      for (size_t y = 0; y < TSZ; y++)
        for (size_t x = 0; x < TSZ / 2; x++)
          ChainSwap2(d[y][x], d[y][TSZ - 1 - x]);

      for (size_t y = 0; y < yspan; y++)
        memcpy(
            img_out.rowPtrMut(yout + y) + (xout + TSZ - xspan) * BPP,
            d[y] + TSZ - xspan, xspan * BPP);
    }
}

template <size_t BPP, size_t TSZ>
void TiledRotate180(
    MutImageView<uint8_t>& img_out, const ImageView<uint8_t>& img_in)
{
  static_assert(!(TSZ & 1), "Tilesize must be even.");

  const size_t w = img_in.width();
  const size_t h = img_in.height();

  typedef struct {
    uint8_t d[BPP];
  } T;
  T d[TSZ][TSZ];

  for (size_t xin = 0; xin < w; xin += TSZ)
    for (size_t yin = 0; yin < h; yin += TSZ) {
      const size_t xspan = std::min(TSZ, w - xin);
      const size_t yspan = std::min(TSZ, h - yin);
      const size_t xout = w - xin - TSZ;
      const size_t yout = h - yin - TSZ;

      for (size_t y = 0; y < yspan; y++)
        memcpy(d[y], img_in.rowPtr(yin + y) + xin * BPP, xspan * BPP);

      for (size_t y = 0; y < TSZ / 2; y++)
        for (size_t x = 0; x < TSZ; x++)
          ChainSwap2(d[y][x], d[TSZ - 1 - y][TSZ - 1 - x]);

      for (size_t y = TSZ - yspan; y < TSZ; y++)
        memcpy(
            img_out.rowPtrMut(yout + y) + (xout + TSZ - xspan) * BPP,
            d[y] + TSZ - xspan, xspan * BPP);
    }
}

template <size_t BPP, size_t TSZ>
void TiledTranspose(
    MutImageView<uint8_t>& img_out, const ImageView<uint8_t>& img_in)
{
  const size_t w = img_in.width();
  const size_t h = img_in.height();

  typedef struct {
    uint8_t d[BPP];
  } T;
  T d[TSZ][TSZ];

  for (size_t xin = 0; xin < w; xin += TSZ)
    for (size_t yin = 0; yin < h; yin += TSZ) {
      const size_t xspan = std::min(TSZ, w - xin);
      const size_t yspan = std::min(TSZ, h - yin);
      const size_t dmin = std::min(xspan, yspan);
      const size_t dmax = std::max(xspan, yspan);
      const size_t xout = yin;
      const size_t yout = xin;

      for (size_t y = 0; y < yspan; y++)
        memcpy(d[y], img_in.rowPtr(yin + y) + xin * BPP, xspan * BPP);

      for (size_t x = 0; x < dmin; x++)
        for (size_t y = x + 1; y < dmax; y++) ChainSwap2(d[x][y], d[y][x]);

      for (size_t y = 0; y < xspan; y++)
        memcpy(img_out.rowPtrMut(yout + y) + xout * BPP, d[y], yspan * BPP);
    }
}

template <size_t BPP, size_t TSZ>
void TiledRotateCW(
    MutImageView<uint8_t>& img_out, const ImageView<uint8_t>& img_in)
{
  static_assert(!(TSZ & 1), "Tilesize must be even.");

  const size_t w = img_in.width();
  const size_t h = img_in.height();

  typedef struct {
    uint8_t d[BPP];
  } T;
  T d[TSZ][TSZ];

  for (size_t xin = 0; xin < w; xin += TSZ)
    for (size_t yin = 0; yin < h; yin += TSZ) {
      const size_t xspan = std::min(TSZ, w - xin);
      const size_t yspan = std::min(TSZ, h - yin);
      const size_t xout = h - yin - TSZ;
      const size_t yout = xin;

      for (size_t y = 0; y < yspan; y++)
        memcpy(d[y], img_in.rowPtr(yin + y) + xin * BPP, xspan * BPP);

      for (size_t y = 0; y < TSZ / 2; y++)
        for (size_t x = 0; x < TSZ / 2; x++)
          ChainSwap4(
              d[TSZ - 1 - x][y], d[TSZ - 1 - y][TSZ - 1 - x], d[x][TSZ - 1 - y],
              d[y][x]);

      for (size_t y = 0; y < xspan; y++)
        memcpy(
            img_out.rowPtrMut(yout + y) + (xout + TSZ - yspan) * BPP,
            d[y] + TSZ - yspan, yspan * BPP);
    }
}

template <size_t BPP, size_t TSZ>
void TiledRotateCCW(
    MutImageView<uint8_t>& img_out, const ImageView<uint8_t>& img_in)
{
  static_assert(!(TSZ & 1), "Tilesize must be even.");

  const size_t w = img_in.width();
  const size_t h = img_in.height();

  typedef struct {
    uint8_t d[BPP];
  } T;
  T d[TSZ][TSZ];

  for (size_t xin = 0; xin < w; xin += TSZ)
    for (size_t yin = 0; yin < h; yin += TSZ) {
      const size_t xspan = std::min(TSZ, w - xin);
      const size_t yspan = std::min(TSZ, h - yin);
      const size_t xout = yin;
      const size_t yout = w - xin - TSZ;

      for (size_t y = 0; y < yspan; y++)
        memcpy(d[y], img_in.rowPtr(yin + y) + xin * BPP, xspan * BPP);

      for (size_t y = 0; y < TSZ / 2; y++)
        for (size_t x = 0; x < TSZ / 2; x++)
          ChainSwap4(
              d[y][x], d[x][TSZ - 1 - y], d[TSZ - 1 - y][TSZ - 1 - x],
              d[TSZ - 1 - x][y]);

      for (size_t y = TSZ - xspan; y < TSZ; y++)
        memcpy(img_out.rowPtrMut(yout + y) + xout * BPP, d[y], yspan * BPP);
    }
}

void FlipX(
    MutImageView<uint8_t>& img_out, const ImageView<uint8_t>& img_in,
    size_t bytes_per_pixel)
{
  if (bytes_per_pixel == 1)
    TiledFlipX<1, 160>(img_out, img_in);
  else if (bytes_per_pixel == 2)
    TiledFlipX<2, 120>(img_out, img_in);
  else if (bytes_per_pixel == 3)
    TiledFlipX<3, 80>(img_out, img_in);
  else if (bytes_per_pixel == 4)
    TiledFlipX<4, 80>(img_out, img_in);
  else if (bytes_per_pixel == 6)
    TiledFlipX<6, 64>(img_out, img_in);
  else {
    for (int y = 0; y < img_out.height(); ++y) {
      for (int x = 0; x < img_out.width(); ++x) {
        memcpy(
            img_out.rowPtrMut((int)y) +
                (img_out.width() - 1 - x) * bytes_per_pixel,
            img_in.rowPtr((int)y) + x * bytes_per_pixel, bytes_per_pixel);
      }
    }
  }
}

void FlipXY(
    MutImageView<uint8_t>& img_out, const ImageView<uint8_t>& img_in,
    size_t bytes_per_pixel)
{
  if (bytes_per_pixel == 1)
    TiledRotate180<1, 160>(img_out, img_in);
  else if (bytes_per_pixel == 2)
    TiledRotate180<2, 120>(img_out, img_in);
  else if (bytes_per_pixel == 3)
    TiledRotate180<3, 80>(img_out, img_in);
  else if (bytes_per_pixel == 4)
    TiledRotate180<4, 80>(img_out, img_in);
  else if (bytes_per_pixel == 6)
    TiledRotate180<6, 64>(img_out, img_in);
  else {
    for (int y_out = 0; y_out < img_out.height(); ++y_out) {
      for (int x = 0; x < img_out.width(); ++x) {
        const size_t y_in = (img_in.height() - 1) - y_out;
        memcpy(
            img_out.rowPtrMut((int)y_out) +
                (img_out.width() - 1 - x) * bytes_per_pixel,
            img_in.rowPtr((int)y_in) + x * bytes_per_pixel, bytes_per_pixel);
      }
    }
  }
}

void RotateCW(
    MutImageView<uint8_t>& img_out, const ImageView<uint8_t>& img_in,
    size_t bytes_per_pixel)
{
  if (bytes_per_pixel == 1)
    TiledRotateCW<1, 160>(img_out, img_in);
  else if (bytes_per_pixel == 2)
    TiledRotateCW<2, 120>(img_out, img_in);
  else if (bytes_per_pixel == 3)
    TiledRotateCW<3, 80>(img_out, img_in);
  else if (bytes_per_pixel == 4)
    TiledRotateCW<4, 80>(img_out, img_in);
  else if (bytes_per_pixel == 6)
    TiledRotateCW<6, 64>(img_out, img_in);
  else {
    for (int yout = 0; yout < img_out.height(); ++yout)
      for (int xout = 0; xout < img_out.width(); ++xout) {
        size_t xin = yout;
        size_t yin = img_out.width() - 1 - xout;
        memcpy(
            img_out.rowPtrMut((int)yout) + xout * bytes_per_pixel,
            img_in.rowPtr((int)yin) + xin * bytes_per_pixel, bytes_per_pixel);
      }
  }
}

void Transpose(
    MutImageView<uint8_t>& img_out, const ImageView<uint8_t>& img_in,
    size_t bytes_per_pixel)
{
  if (bytes_per_pixel == 1)
    TiledTranspose<1, 160>(img_out, img_in);
  else if (bytes_per_pixel == 2)
    TiledTranspose<2, 120>(img_out, img_in);
  else if (bytes_per_pixel == 3)
    TiledTranspose<3, 80>(img_out, img_in);
  else if (bytes_per_pixel == 4)
    TiledTranspose<4, 80>(img_out, img_in);
  else if (bytes_per_pixel == 6)
    TiledTranspose<6, 64>(img_out, img_in);
  else {
    for (int yout = 0; yout < img_out.height(); ++yout)
      for (int xout = 0; xout < img_out.width(); ++xout) {
        size_t xin = yout;
        size_t yin = xout;
        memcpy(
            img_out.rowPtrMut((int)yout) + xout * bytes_per_pixel,
            img_in.rowPtr((int)yin) + xin * bytes_per_pixel, bytes_per_pixel);
      }
  }
}

void RotateCCW(
    MutImageView<uint8_t>& img_out, const ImageView<uint8_t>& img_in,
    size_t bytes_per_pixel)
{
  if (bytes_per_pixel == 1)
    TiledRotateCCW<1, 160>(img_out, img_in);
  else if (bytes_per_pixel == 2)
    TiledRotateCCW<2, 120>(img_out, img_in);
  else if (bytes_per_pixel == 3)
    TiledRotateCCW<3, 80>(img_out, img_in);
  else if (bytes_per_pixel == 4)
    TiledRotateCCW<4, 80>(img_out, img_in);
  else if (bytes_per_pixel == 6)
    TiledRotateCCW<6, 64>(img_out, img_in);
  else {
    for (int yout = 0; yout < img_out.height(); ++yout)
      for (int xout = 0; xout < img_out.width(); ++xout) {
        size_t xin = img_out.height() - 1 - yout;
        size_t yin = xout;
        memcpy(
            img_out.rowPtrMut((int)yout) + xout * bytes_per_pixel,
            img_in.rowPtr((int)yin) + xin * bytes_per_pixel, bytes_per_pixel);
      }
  }
}

void TransformVideo::Process(uint8_t* buffer_out, const uint8_t* buffer_in)
{
  for (size_t s = 0; s < streams.size(); ++s) {
    MutImageView<uint8_t> img_out = Streams()[s].StreamImage(buffer_out);
    const ImageView<uint8_t> img_in =
        videoin->Streams()[s].StreamImage(buffer_in);
    const size_t bytes_per_pixel = Streams()[s].format().bytesPerPixel();

    switch (flips[s]) {
      case TransformOptions::FlipX:
        FlipX(img_out, img_in, bytes_per_pixel);
        break;
      case TransformOptions::FlipY:
        FlipY(img_out, img_in, bytes_per_pixel);
        break;
      case TransformOptions::FlipXY:
        FlipXY(img_out, img_in, bytes_per_pixel);
        break;
      case TransformOptions::RotateCW:
        RotateCW(img_out, img_in, bytes_per_pixel);
        break;
      case TransformOptions::RotateCCW:
        RotateCCW(img_out, img_in, bytes_per_pixel);
        break;
      case TransformOptions::Transpose:
        Transpose(img_out, img_in, bytes_per_pixel);
        break;
      case TransformOptions::None:
        PitchedImageCopy(img_out, img_in, bytes_per_pixel);
        break;
      default:
        PANGO_WARN(
            "TransformVideo::Process(): Invalid enum {}.\n", int(flips[s]));
        break;
    }
  }
}

//! Implement VideoInput::GrabNext()
bool TransformVideo::GrabNext(uint8_t* image, bool wait)
{
  if (videoin->GrabNext(buffer, wait)) {
    Process(image, buffer);
    return true;
  } else {
    return false;
  }
}

//! Implement VideoInput::GrabNewest()
bool TransformVideo::GrabNewest(uint8_t* image, bool wait)
{
  if (videoin->GrabNewest(buffer, wait)) {
    Process(image, buffer);
    return true;
  } else {
    return false;
  }
}

std::vector<VideoInterface*>& TransformVideo::InputStreams() { return inputs; }

unsigned int TransformVideo::AvailableFrames() const
{
  BufferAwareVideoInterface* vpi =
      dynamic_cast<BufferAwareVideoInterface*>(videoin.get());
  if (!vpi) {
    PANGO_WARN("Mirror: child interface is not buffer aware.");
    return 0;
  } else {
    return vpi->AvailableFrames();
  }
}

bool TransformVideo::DropNFrames(uint32_t n)
{
  BufferAwareVideoInterface* vpi =
      dynamic_cast<BufferAwareVideoInterface*>(videoin.get());
  if (!vpi) {
    PANGO_WARN("Mirror: child interface is not buffer aware.");
    return false;
  } else {
    return vpi->DropNFrames(n);
  }
}

const std::map<std::string, TransformOptions> StringToMirrorOptionMap = {
    {"none", TransformOptions::None},
    {"transform", TransformOptions::None},
    {"mirror", TransformOptions::FlipX},
    {"flipx", TransformOptions::FlipX},
    {"flip", TransformOptions::FlipY},
    {"flipy", TransformOptions::FlipY},
    {"flipxy", TransformOptions::FlipXY},
    {"transpose", TransformOptions::Transpose},
    {"rotate", TransformOptions::RotateCW},
    {"rotatecw", TransformOptions::RotateCW},
    {"rotateccw", TransformOptions::RotateCCW}};

std::istream& operator>>(std::istream& is, TransformOptions& mirror)
{
  std::string str_mirror;
  is >> str_mirror;

  auto default_it = StringToMirrorOptionMap.find(str_mirror);
  mirror = (default_it != StringToMirrorOptionMap.end())
               ? default_it->second
               : TransformOptions::None;

  return is;
}

PANGOLIN_REGISTER_FACTORY(TransformVideo)
{
  struct TransformVideoFactory final
      : public TypedFactoryInterface<VideoInterface> {
    std::map<std::string, Precedence> Schemes() const override
    {
      return {{"transform", 10}, {"mirror", 10},    {"flip", 10},
              {"rotate", 10},    {"transpose", 10}, {"rotatecw", 10},
              {"rotateccw", 10}, {"flipx", 10},     {"flipy", 10},
              {"flipxy", 10}};
    }
    const char* Description() const override
    {
      return "Filter: Apply one of a number of simple image transforms to the "
             "streams.";
    }
    ParamSet Params() const override
    {
      return {
          {{"stream\\d+", "none (or scheme name)",
            "Transform to apply to stream. One of "
            "(None,FlipX,FlipY,FlipXY,Transpose,RotateCW,RotateCCW)."}}};
    }
    std::unique_ptr<VideoInterface> Open(const Uri& uri) override
    {
      std::unique_ptr<VideoInterface> subvid = pangolin::OpenVideo(uri.url);
      auto default_it = StringToMirrorOptionMap.find(uri.scheme);
      const TransformOptions default_transform =
          (default_it != StringToMirrorOptionMap.end())
              ? default_it->second
              : TransformOptions::None;

      ParamReader reader(Params(), uri);
      std::vector<TransformOptions> transforms;

      for (size_t i = 0; i < subvid->Streams().size(); ++i) {
        std::stringstream ss;
        ss << "stream" << i;
        const std::string key = ss.str();
        transforms.push_back(
            reader.Get<TransformOptions>(key, default_transform));
      }

      return std::unique_ptr<VideoInterface>(
          new TransformVideo(subvid, transforms));
    }
  };

  return FactoryRegistry::I()->RegisterFactory<VideoInterface>(
      std::make_shared<TransformVideoFactory>());
}

}  // namespace pangolin
