/* ui_text's multi-channel signed distance fields: see ui_msdf.h. stb_truetype's outline (lines, quadratic and cubic
 * curves) is a msdfgen Shape; its edges are coloured (edgeColoringSimple, 3 radians), the field generated with
 * msdfgen's error correction and overlap support, and its pixels stored in stbtt_GetGlyphSDF's layout. */
#include "ui_msdf.h"
#include "msdfgen.h"
#include <cmath>
#include <cstdlib>

using namespace msdfgen;

enum { VMOVE = 1, VLINE = 2, VCURVE = 3, VCUBIC = 4 };

extern "C" unsigned char* pc_msdf_glyph(const pc_msdf_vertex* vertices, int count, float scale, int ix0, int iy0, int ix1,
    int iy1, int padding, unsigned char onedge, float pixel_dist_scale)
{
    int w = ix1 - ix0, h = iy1 - iy0;
    if (w <= 0 || h <= 0 || count <= 0)
        return nullptr;
    Shape shape;
    Contour* contour = nullptr;
    Point2 pen;
    for (int i = 0; i < count; i++) {
        const pc_msdf_vertex& v = vertices[i];
        Point2 to(v.x, v.y);
        switch (v.type) {
        case VMOVE:
            contour = &shape.addContour();
            pen = to;
            break;
        case VLINE:
            if (contour)
                contour->addEdge(EdgeHolder(pen, to));
            pen = to;
            break;
        case VCURVE:
            if (contour)
                contour->addEdge(EdgeHolder(pen, Point2(v.cx, v.cy), to));
            pen = to;
            break;
        case VCUBIC:
            if (contour)
                contour->addEdge(EdgeHolder(pen, Point2(v.cx, v.cy), Point2(v.cx1, v.cy1), to));
            pen = to;
            break;
        }
    }
    shape.normalize();
    if (shape.contours.empty() || !shape.validate())
        return nullptr;
    edgeColoringSimple(shape, 3.0);
    /* The field's pixel (x, y) from the top is the point ((xoff + x + 0.5) / scale, -(yoff + y + 0.5) / scale) of the
     * glyph (font units, y up): msdfgen's bitmaps run bottom up, its projection is pixel = scale * (point + translate). */
    double s = scale;
    Vector2 translate(-ix0 / s, (h + iy0) / s);
    Bitmap<float, 3> field(w, h);
    generateMSDF(field, shape, Range(2.0 * padding / s), Vector2(s, s), translate);
    unsigned char* bytes = static_cast<unsigned char*>(std::malloc((size_t)w * (size_t)h * 3));
    if (!bytes)
        return nullptr;
    /* byte = onedge + distance in pixels * pixel_dist_scale; msdfgen's value is 0.5 + distance / range */
    double per_value = 2.0 * padding * pixel_dist_scale;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++) {
                double value = field(x, h - 1 - y)[c];
                double byte = onedge + (value - 0.5) * per_value;
                bytes[((size_t)y * (size_t)w + (size_t)x) * 3 + (size_t)c] = (unsigned char)(byte < 0 ? 0 : byte > 255 ? 255 : (int)(byte + 0.5));
            }
    return bytes;
}
