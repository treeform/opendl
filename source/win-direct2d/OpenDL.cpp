#include "../opendl.h"

#include <assert.h>

#include <vector>
#include <map>
#include <limits>

#include "private_defs.h" // content for opaque handles
#include "unicodestuff.h"

#include "directwrite/CoreTextFormatSpec.h"
#include "directwrite/CoreTextRenderer.h"

#include "COMStuff.h"

#include "../common/geometry.h"

#include "../dlcf.h"

#include "../common/classes/CF/CFTypes.h"

#include "classes/CG/CGContext.h"
#include "classes/CG/CGBitmapContext.h"
#include "classes/CG/CGImage.h"
#include "classes/CG/CGColor.h"
#include "classes/CG/CGPath.h"
#include "classes/CG/CGColorSpace.h"
#include "classes/CG/CGGradient.h"

#include "classes/CT/CTFont.h"
#include "classes/CT/CTFontDescriptor.h"
#include "classes/CT/CTFrameSetter.h"
#include "classes/CT/CTFrame.h"
#include "classes/CT/CTLine.h"
#include "classes/CT/CTRun.h"
#include "classes/CT/CTParagraphStyle.h"

#include <wincodec.h> // for IWICBitmap stuff

#include "directwrite/DWriteFontUtils.h" // createFontMap

// GLOBALS ===================================================================
ID2D1Factory *d2dFactory = nullptr;
IDWriteFactory *writeFactory = nullptr;
IWICImagingFactory *wicFactory = nullptr;

// ENTRY POINT
OPENDL_API int CDECL dl_Init(struct DLPlatformOptions *options)
{
	d2dFactory = options->factory;
	d2dFactory->AddRef();

	HR(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&writeFactory)));

	HR(CoCreateInstance(
		CLSID_WICImagingFactory,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory,
		(LPVOID *)&wicFactory));

	//createFontMap();

	return 0;
}

OPENDL_API void CDECL dl_Shutdown()
{
	wicFactory->Release();
	writeFactory->Release();
	d2dFactory->Release();
}

// VARIOUS CONVERSION FUNCTIONS (FROM QUARTZ TO NATIVE AND BACK, ETC) ========

// (moved to private_defs.h)

// POINT / RECT STUFF ========================================================

OPENDL_API const dl_CFIndex dl_kCFNotFound = -1;
OPENDL_API const dl_CFRange dl_CFRangeZero = dl_CFRangeMake(0, 0);
OPENDL_API const dl_CGPoint dl_CGPointZero = dl_CGPointMake(0, 0);
OPENDL_API const dl_CGRect dl_CGRectZero = dl_CGRectMake(0, 0, 0, 0);

// TRANSFORM STUFF ===========================================================

OPENDL_API const dl_CGAffineTransform dl_CGAffineTransformIdentity = { 1, 0, 0, 1, 0, 0 }; // a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0
OPENDL_API dl_CGAffineTransform CDECL dl_CGAffineTransformTranslate(dl_CGAffineTransform t, dl_CGFloat tx, dl_CGFloat ty)
{
	auto d2d_m = d2dMatrixFromDLAffineTransform(t);
	auto xlat = D2D1::Matrix3x2F::Translation(D2D1::SizeF((FLOAT)tx, (FLOAT)ty));
	return dlAffineTransformFromD2DMatrix(xlat * d2d_m);
}

OPENDL_API dl_CGAffineTransform CDECL dl_CGAffineTransformRotate(dl_CGAffineTransform t, dl_CGFloat angle)
{
	auto d2d_m = d2dMatrixFromDLAffineTransform(t);
	auto rot = D2D1::Matrix3x2F::Rotation(radiansToDegrees(angle));
	return dlAffineTransformFromD2DMatrix(rot * d2d_m);
}

OPENDL_API dl_CGAffineTransform CDECL dl_CGAffineTransformScale(dl_CGAffineTransform t, dl_CGFloat sx, dl_CGFloat sy)
{
	auto d2d_m = d2dMatrixFromDLAffineTransform(t);
	auto scale = D2D1::Matrix3x2F::Scale(D2D1::SizeF((FLOAT)sx, (FLOAT)sy));
	return dlAffineTransformFromD2DMatrix(scale * d2d_m);
}

OPENDL_API dl_CGAffineTransform CDECL dl_CGAffineTransformConcat(dl_CGAffineTransform t1, dl_CGAffineTransform t2)
{
	auto d2d_m1 = d2dMatrixFromDLAffineTransform(t1);
	auto d2d_m2 = d2dMatrixFromDLAffineTransform(t2);
	printf("use of dl_CGAffineTransformConcat in win32 -- please verify results, might have the order wrong\n");
	return dlAffineTransformFromD2DMatrix(d2d_m2 * d2d_m1); // or no?
}

// DLL LOAD/UNLOAD STUFF ===================================

void dlPrivateInit() {
	printf("hello from OpenDL private init\n");
	// nothing yet
}

void dlPrivateShutdown() {
	printf("hello from OpenDL private shutdown\n");
	// nothing yet
}

// CONTEXT METHODS

OPENDL_API dl_CGContextRef CDECL dl_CGContextCreateD2D(ID2D1RenderTarget *target)
{
	//activeTarget = target;
	// don't AddRef, that's all handled by wherever we're getting these from
	return (dl_CGContextRef) new CGContext(target);
}

OPENDL_API void CDECL dl_D2DTargetRecreated(ID2D1RenderTarget *newTarget, ID2D1RenderTarget *oldTarget)
{
	createCacheForTarget(newTarget, oldTarget);
	//activeTarget = newTarget;
}

OPENDL_API void dl_CGContextRelease(dl_CGContextRef c)
{
	((CGContextRef)c)->release();
}

OPENDL_API void CDECL dl_CGContextSetRGBFillColor(dl_CGContextRef c, dl_CGFloat red, dl_CGFloat green, dl_CGFloat blue, dl_CGFloat alpha)
{
	((CGContextRef)c)->setRGBFillColor(red, green, blue, alpha);
}

OPENDL_API void CDECL dl_CGContextFillRect(dl_CGContextRef c, dl_CGRect rect)
{
	((CGContextRef)c)->fillRect(rect);
}

OPENDL_API void CDECL dl_CGContextSetRGBStrokeColor(dl_CGContextRef c, dl_CGFloat red, dl_CGFloat green, dl_CGFloat blue, dl_CGFloat alpha)
{
	((CGContextRef)c)->setRGBStrokeColor(red, green, blue, alpha);
}

OPENDL_API void CDECL dl_CGContextStrokeRect(dl_CGContextRef c, dl_CGRect rect)
{
	((CGContextRef)c)->strokeRect(rect);
}

OPENDL_API void CDECL dl_CGContextStrokeRectWithWidth(dl_CGContextRef c, dl_CGRect rect, dl_CGFloat width)
{
	((CGContextRef)c)->strokeRectWithWidth(rect, width);
}

OPENDL_API void CDECL dl_CGContextBeginPath(dl_CGContextRef c)
{
	((CGContextRef)c)->beginPath();
}

OPENDL_API void CDECL dl_CGContextClosePath(dl_CGContextRef c)
{
	((CGContextRef)c)->closePath();
}

OPENDL_API void CDECL dl_CGContextMoveToPoint(dl_CGContextRef c, dl_CGFloat x, dl_CGFloat y)
{
	((CGContextRef)c)->moveToPoint(x, y);
}

OPENDL_API void CDECL dl_CGContextAddLineToPoint(dl_CGContextRef c, dl_CGFloat x, dl_CGFloat y)
{
	((CGContextRef)c)->addLineToPoint(x, y);
}

OPENDL_API void CDECL dl_CGContextAddRect(dl_CGContextRef c, dl_CGRect rect)
{
	((CGContextRef)c)->addRect(rect);
}

OPENDL_API void CDECL dl_CGContextAddArc(dl_CGContextRef c, dl_CGFloat x, dl_CGFloat y, dl_CGFloat radius, dl_CGFloat startAngle, dl_CGFloat endAngle, int clockwise)
{
	((CGContextRef)c)->addArc(x, y, radius, startAngle, endAngle, clockwise);
}

OPENDL_API void CDECL dl_CGContextSetLineWidth(dl_CGContextRef c, dl_CGFloat width)
{
	((CGContextRef)c)->setLineWidth(width);
}

OPENDL_API void CDECL dl_CGContextSetLineDash(dl_CGContextRef c, dl_CGFloat phase, const dl_CGFloat *lengths, size_t count)
{
	((CGContextRef)c)->setLineDash(phase, lengths, count);
}

OPENDL_API void CDECL dl_CGContextDrawPath(dl_CGContextRef c, dl_CGPathDrawingMode mode)
{
	((CGContextRef)c)->drawPath(mode);
}

OPENDL_API void CDECL dl_CGContextStrokePath(dl_CGContextRef c)
{
	((CGContextRef)c)->strokePath();
}

OPENDL_API void CDECL dl_CGContextTranslateCTM(dl_CGContextRef c, dl_CGFloat tx, dl_CGFloat ty)
{
	((CGContextRef)c)->translateCTM(tx, ty);
}

OPENDL_API void CDECL dl_CGContextRotateCTM(dl_CGContextRef c, dl_CGFloat angle)
{
	((CGContextRef)c)->rotateCTM(angle);
}

OPENDL_API void CDECL dl_CGContextScaleCTM(dl_CGContextRef c, dl_CGFloat scaleX, dl_CGFloat scaleY)
{
	((CGContextRef)c)->scaleCTM(scaleX, scaleY);
}

OPENDL_API void CDECL dl_CGContextConcatCTM(dl_CGContextRef c, dl_CGAffineTransform transform)
{
	((CGContextRef)c)->concatCTM(transform);
}

OPENDL_API void CDECL dl_CGContextClip(dl_CGContextRef c)
{
	((CGContextRef)c)->clipCurrentPath();
}

OPENDL_API void CDECL dl_CGContextClipToRect(dl_CGContextRef c, dl_CGRect rect)
{
	((CGContextRef)c)->clipToRect(rect);
}

OPENDL_API void dl_CGContextSaveGState(dl_CGContextRef c)
{
	((CGContextRef)c)->saveGState();
}

OPENDL_API void dl_CGContextRestoreGState(dl_CGContextRef c)
{
	((CGContextRef)c)->restoreGState();
}

// path stuff =======================

OPENDL_API dl_CGMutablePathRef CDECL dl_CGPathCreateMutable(void)
{
	return (dl_CGMutablePathRef) new CGMutablePath();
}

OPENDL_API void CDECL dl_CGPathAddRect(dl_CGMutablePathRef path, const dl_CGAffineTransform *m, dl_CGRect rect)
{
	((CGMutablePathRef)path)->addRect(m, rect);
}

OPENDL_API dl_CGPathRef CDECL dl_CGPathCreateWithRect(dl_CGRect rect, const dl_CGAffineTransform *transform)
{
	return (dl_CGPathRef) new CGPath(rect, transform);
}

OPENDL_API void CDECL dl_CGPathRelease(dl_CGPathRef path)
{
	((CGPathRef)path)->release();
}

OPENDL_API void CDECL dl_CGContextAddPath(dl_CGContextRef context, dl_CGPathRef path)
{
	((CGPathRef)path)->addToContext(context);
}

// color spaces =====================

OPENDL_API const dl_CFStringRef dl_kCGColorSpaceGenericGray = dl_CFSTR("dl_kCGColorSpaceGenericGray");
OPENDL_API const dl_CFStringRef dl_kCGColorSpaceGenericRGB = dl_CFSTR("dl_kCGColorSpaceGenericRGB");
OPENDL_API const dl_CFStringRef dl_kCGColorSpaceGenericCMYK = dl_CFSTR("dl_kCGColorSpaceGenericCMYK");
OPENDL_API const dl_CFStringRef dl_kCGColorSpaceGenericRGBLinear = dl_CFSTR("dl_kCGColorSpaceGenericRGBLinear");
OPENDL_API const dl_CFStringRef dl_kCGColorSpaceAdobeRGB1998 = dl_CFSTR("dl_kCGColorSpaceAdobeRGB1998");
OPENDL_API const dl_CFStringRef dl_kCGColorSpaceSRGB = dl_CFSTR("dl_kCGColorSpaceSRGB");
OPENDL_API const dl_CFStringRef dl_kCGColorSpaceGenericGrayGamma2_2 = dl_CFSTR("dl_kCGColorSpaceGenericGrayGamma2_2");

OPENDL_API dl_CGColorSpaceRef CDECL dl_CGColorSpaceCreateWithName(dl_CFStringRef name)
{
	return (dl_CGColorSpaceRef)new CGColorSpace((cf::StringRef)name);
}

OPENDL_API dl_CGColorSpaceRef CDECL dl_CGColorSpaceCreateDeviceGray(void)
{
	return (dl_CGColorSpaceRef)new CGColorSpace(CGColorSpace::kCGColorSpace__InternalDeviceGray);
}

OPENDL_API void CDECL dl_CGColorSpaceRelease(dl_CGColorSpaceRef colorSpace)
{
	((CGColorSpaceRef)colorSpace)->release();
}

// colors ==========================

OPENDL_API dl_CGColorRef CDECL dl_CGColorCreateGenericRGB(dl_CGFloat red, dl_CGFloat green, dl_CGFloat blue, dl_CGFloat alpha)
{
	return (dl_CGColorRef) new CGColor(D2D1::ColorF((FLOAT)red, (FLOAT)green, (FLOAT)blue, (FLOAT)alpha));
}

OPENDL_API void CDECL dl_CGColorRelease(dl_CGColorRef color)
{
	((CGColorRef)color)->release();
}

OPENDL_API void CDECL dl_CGContextSetFillColorWithColor(dl_CGContextRef c, dl_CGColorRef color)
{
	((CGContextRef)c)->setFillColorWithColor(color);
}

OPENDL_API void CDECL dl_CGContextSetStrokeColorWithColor(dl_CGContextRef c, dl_CGColorRef color)
{
	((CGContextRef)c)->setStrokeColorWithColor(color);
}

OPENDL_API const dl_CFStringRef dl_kCGColorWhite = dl_CFSTR("dl_kCGColorWhite");
OPENDL_API const dl_CFStringRef dl_kCGColorBlack = dl_CFSTR("dl_kCGColorBlack");
OPENDL_API const dl_CFStringRef dl_kCGColorClear = dl_CFSTR("dl_kCGColorClear");

OPENDL_API dl_CGColorRef CDECL dl_CGColorGetConstantColor(dl_CFStringRef colorName)
{
	if (colorName == dl_kCGColorWhite) {
		return (dl_CGColorRef)CGColor::White;
	}
	else if (colorName == dl_kCGColorBlack) {
		return (dl_CGColorRef)CGColor::Black;
	}
	else if (colorName == dl_kCGColorClear) {
		return (dl_CGColorRef)CGColor::Clear;
	}
	else {
		auto name_str = ((cf::StringRef)colorName)->toString();
		printf("dl_CGColorGetConstantColor called with unknown color name (%s)", name_str.c_str());
		return nullptr;
	}
}

// gradient stuff

OPENDL_API dl_CGGradientRef CDECL dl_CGGradientCreateWithColorComponents(dl_CGColorSpaceRef space, const dl_CGFloat components[], const dl_CGFloat locations[], size_t count)
{
	return (dl_CGGradientRef)new CGGradient((CGColorSpaceRef)space, components, locations, count);
}

OPENDL_API void CDECL dl_CGGradientRelease(dl_CGGradientRef gradient)
{
	((CGGradientRef)gradient)->release();
}

OPENDL_API void CDECL dl_CGContextDrawLinearGradient(dl_CGContextRef c, dl_CGGradientRef gradient, dl_CGPoint startPoint, dl_CGPoint endPoint, dl_CGGradientDrawingOptions options)
{
	((CGContextRef)c)->drawLinearGradient(gradient, startPoint, endPoint, options);
}

// TEXT STUFF ==================================================================

OPENDL_API const dl_CFStringRef dl_kCTForegroundColorAttributeName = dl_CFSTR("dl_kCTForegroundColorAttributeName");
OPENDL_API const dl_CFStringRef dl_kCTForegroundColorFromContextAttributeName = dl_CFSTR("dl_kCTForegroundColorFromContextAttributeName");
OPENDL_API const dl_CFStringRef dl_kCTFontAttributeName = dl_CFSTR("dl_kCTFontAttributeName");
OPENDL_API const dl_CFStringRef dl_kCTStrokeWidthAttributeName = dl_CFSTR("dl_kCTStrokeWidthAttributeName");
OPENDL_API const dl_CFStringRef dl_kCTStrokeColorAttributeName = dl_CFSTR("dl_kCTStrokeColorAttributeName");

OPENDL_API void dl_CGContextSetTextMatrix(dl_CGContextRef c, dl_CGAffineTransform t)
{
	((CGContextRef)c)->setTextMatrix(t);
}

OPENDL_API dl_CTFontRef CDECL dl_CTFontCreateWithName(dl_CFStringRef name, dl_CGFloat size, const dl_CGAffineTransform *matrix)
{
	return (dl_CTFontRef)CTFont::createWithName((cf::StringRef)name, size, matrix);
}

OPENDL_API dl_CFArrayRef CDECL dl_CTFontManagerCreateFontDescriptorsFromURL(dl_CFURLRef fileURL)
{
	return (dl_CFArrayRef) CTFontDescriptor::createDescriptorsFromURL((cf::URLRef) fileURL);
}

OPENDL_API dl_CTFontRef CDECL dl_CTFontCreateWithFontDescriptor(dl_CTFontDescriptorRef descriptor, dl_CGFloat size, const dl_CGAffineTransform *matrix)
{
	return (dl_CTFontRef)CTFont::createWithFontDescriptor((CTFontDescriptorRef)descriptor, size, matrix);
}

OPENDL_API dl_CTFontRef CDECL dl_CTFontCreateCopyWithSymbolicTraits(dl_CTFontRef font, dl_CGFloat size, const dl_CGAffineTransform *matrix, dl_CTFontSymbolicTraits symTraitValue, dl_CTFontSymbolicTraits symTraitMask)
{
	return (dl_CTFontRef)((CTFontRef)font)->copyWithSymbolicTraits(size, matrix, symTraitValue, symTraitMask);
}

OPENDL_API dl_CGFloat CDECL dl_CTFontGetUnderlineThickness(dl_CTFontRef font)
{
	return ((CTFontRef)font)->getUnderlineThickness();
}

OPENDL_API dl_CGFloat CDECL dl_CTFontGetUnderlinePosition(dl_CTFontRef font)
{
	auto ulPos = ((CTFontRef)font)->getUnderlinePosition();
	//printf("ulPos is: %.2f\n", ulPos);
	return ulPos;
}

OPENDL_API dl_CTFramesetterRef CDECL dl_CTFramesetterCreateWithAttributedString(dl_CFAttributedStringRef attrString, int viewHeight)
{
	// we don't currently use the viewHeight argument - was placed there (breaking the Quartz API a bit) to allow for flipped text rendering on OSX
	return (dl_CTFramesetterRef)CTFrameSetter::createWithAttributedString((cf::AttributedStringRef)attrString);
}

OPENDL_API dl_CTFrameRef CDECL dl_CTFramesetterCreateFrame(dl_CTFramesetterRef framesetter, dl_CFRange stringRange, dl_CGPathRef path, dl_CFDictionaryRef frameAttributes)
{
	return (dl_CTFrameRef)((CTFrameSetterRef)framesetter)->createFrame(stringRange, (CGPathRef)path, (cf::DictionaryRef)frameAttributes);
}

OPENDL_API dl_CFArrayRef CDECL dl_CTFrameGetLines(dl_CTFrameRef frame)
{
	return (dl_CFArrayRef)((CTFrameRef)frame)->getLines();
}

OPENDL_API void CDECL dl_CTFrameGetLineOrigins(dl_CTFrameRef frame, dl_CFRange range, dl_CGPoint origins[])
{
	((CTFrameRef)frame)->getLineOrigins(range, origins);
}

OPENDL_API dl_CFDictionaryRef CDECL dl_CTRunGetAttributes(dl_CTRunRef run)
{
	return (dl_CFDictionaryRef)((CTRunRef)run)->getAttributes();
}

OPENDL_API double CDECL dl_CTRunGetTypographicBounds(dl_CTRunRef run, dl_CFRange range, dl_CGFloat* ascent, dl_CGFloat* descent, dl_CGFloat* leading)
{
	return ((CTRunRef)run)->getTypographicBounds(range, ascent, descent, leading);
}

OPENDL_API dl_CFRange CDECL dl_CTRunGetStringRange(dl_CTRunRef run)
{
	return ((CTRunRef)run)->getStringRange();
}

OPENDL_API dl_CTRunStatus CDECL dl_CTRunGetStatus(dl_CTRunRef run)
{
	return ((CTRunRef)run)->getStatus();
}

OPENDL_API void CDECL dl_CTFrameDraw(dl_CTFrameRef frame, dl_CGContextRef context)
{
	return ((CTFrameRef)frame)->draw((CGContextRef)context);
}

OPENDL_API dl_CTParagraphStyleRef CDECL dl_CTParagraphStyleCreate(const dl_CTParagraphStyleSetting * settings, size_t settingCount)
{
	return (dl_CTParagraphStyleRef) new CTParagraphStyle(settings, settingCount);
}

OPENDL_API const dl_CFStringRef dl_kCTParagraphStyleAttributeName = dl_CFSTR("dl_kCTParagraphStyleAttributeName");

OPENDL_API dl_CTLineRef CDECL dl_CTLineCreateWithAttributedString(dl_CFAttributedStringRef string)
{
	return (dl_CTLineRef)CTLine::createFromAttrString((cf::AttributedStringRef)string);
}

OPENDL_API double CDECL dl_CTLineGetTypographicBounds(dl_CTLineRef line, dl_CGFloat *ascent, dl_CGFloat *descent, dl_CGFloat *leading)
{
	return ((CTLineRef)line)->getTypographicBounds(ascent, descent, leading);
}

OPENDL_API dl_CGFloat CDECL dl_CTLineGetOffsetForStringIndex(dl_CTLineRef line, dl_CFIndex charIndex, dl_CGFloat* secondaryOffset)
{
	return ((CTLineRef)line)->getOffsetForStringIndex(charIndex, secondaryOffset);
}

OPENDL_API dl_CFIndex CDECL dl_CTLineGetStringIndexForPosition(dl_CTLineRef line, dl_CGPoint position)
{
	return ((CTLineRef)line)->getStringIndexForPosition(position);
}

OPENDL_API dl_CFRange CDECL dl_CTLineGetStringRange(dl_CTLineRef line)
{
	return ((CTLineRef)line)->getStringRange();
}

OPENDL_API dl_CGRect CDECL dl_CTLineGetBoundsWithOptions(dl_CTLineRef line, dl_CTLineBoundsOptions options)
{
	return ((CTLineRef)line)->getBoundsWithOptions(options);
}

OPENDL_API dl_CFArrayRef CDECL dl_CTLineGetGlyphRuns(dl_CTLineRef line)
{
	return (dl_CFArrayRef)((CTLineRef)line)->getGlyphRuns();
}

OPENDL_API void CDECL dl_CTLineDraw(dl_CTLineRef line, dl_CGContextRef context)
{
	((CTLineRef)line)->draw((CGContextRef)context);
}

OPENDL_API void CDECL dl_CGContextSetTextPosition(dl_CGContextRef c, dl_CGFloat x, dl_CGFloat y)
{
	((CGContextRef)c)->setTextPosition(x, y);
}

OPENDL_API void CDECL dl_CGContextSetTextDrawingMode(dl_CGContextRef c, dl_CGTextDrawingMode mode)
{
	((CGContextRef)c)->setTextDrawingMode(mode);
}

OPENDL_API dl_CGContextRef CDECL dl_CGBitmapContextCreate(
	void *data, size_t width,
	size_t height, size_t bitsPerComponent, size_t bytesPerRow,
	dl_CGColorSpaceRef space, dl_CGBitmapInfo bitmapInfo)
{
	if ((bitmapInfo & dl_kCGBitmapByteOrderMask) != dl_kCGBitmapByteOrderDefault) {
		printf("dl_CGBitmapContextCreate only supports dl_kCGBitmapByteOrderDefault at the moment\n");
		return nullptr;
	}
	if (((CGColorSpaceRef)space)->getColorSpaceName() != CGColorSpace::kCGColorSpace__InternalDeviceGray) {
		printf("dl_CGBitmapContextCreate only supports Device Gray color space at the moment\n");
		return nullptr;
	}

	// see https://docs.microsoft.com/en-us/windows/desktop/Direct2D/supported-pixel-formats-and-alpha-modes
	//   for information about compatibility between WICBitmap and D2D render target pixel formats
	// short version, use GUID_WICPixelFormat32bppPBGRA and DXGI_FORMAT_B8G8R8A8_UNORM :)

	IWICBitmap *wicBitmap;
	HR(wicFactory->CreateBitmap(
		(UINT)width,
		(UINT)height,
		GUID_WICPixelFormat32bppPBGRA,
		WICBitmapCacheOnDemand, // WICBitmapCacheOnLoad, //WICBitmapCacheOnDemand,
		&wicBitmap));

	auto props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT, //D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
	); // no explicit DPI for the moment
	//	0, 0,
	//	D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE //  D2D1_RENDER_TARGET_USAGE_NONE,
	//	//D2D1_FEATURE_LEVEL_DEFAULT
	//);
	
	ID2D1RenderTarget *wicRenderTarget;
	HR(d2dFactory->CreateWicBitmapRenderTarget(
		wicBitmap,
		props,
		&wicRenderTarget
	));

	::createCacheForTarget(wicRenderTarget, nullptr);

	return (dl_CGContextRef) new CGBitmapContext(wicRenderTarget, wicBitmap); // ownership passes in
}

OPENDL_API dl_CGImageRef CDECL dl_CGBitmapContextCreateImage(dl_CGContextRef context)
{
	return (dl_CGImageRef)((CGBitmapContextRef)context)->createImage();
}

OPENDL_API void dl_CGImageRelease(dl_CGImageRef image)
{
	((CGImageRef)image)->release();
}

OPENDL_API void CDECL dl_CGContextClipToMask(dl_CGContextRef c, dl_CGRect rect, dl_CGImageRef mask)
{
	((CGContextRef)c)->clipToMask(rect, (CGImageRef)mask);
}

OPENDL_API void CDECL dl_CGContextDrawImage(dl_CGContextRef c, dl_CGRect rect, dl_CGImageRef image)
{
	((CGContextRef)c)->drawImage(rect, (CGImageRef)image);
}

OPENDL_API void CDECL dl_InternalTesting(dl_CGContextRef c, int width, int height)
{
	//
}

//device - independent resources :
//============================ =
//-WICImagingFactory
//- Direct2D factory
//- DirectWrite factory(DWRITE_FACTORY_TYPE_SHARED ... matters ? )
//- any textformats / layouts
//
//device dependent resources :
//============================ =
//- render target
//- brushes
//- custom text renderer
