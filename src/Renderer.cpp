#include <EStudioUI/Renderer.h>

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace EStudioUI
{
namespace
{
// One ordered instance stream lets backgrounds, shadows, glyphs, selection,
// and carets share one draw call without reordering translucent primitives.
const char* UnifiedVertex = R"GLSL(#version 330 core
layout(location=0) in vec4 aRect;
layout(location=1) in vec4 aAux;
layout(location=2) in vec4 aColor;
layout(location=3) in vec4 aBorder;
layout(location=4) in vec4 aClip;
layout(location=5) in vec4 aBands;
layout(location=6) in ivec4 aData;
uniform vec2 uViewport;
out vec2 vEm;
out vec2 vPosition;
flat out vec4 vRect;
flat out vec4 vParams;
flat out vec4 vColor;
flat out vec4 vBorder;
flat out vec4 vClip;
flat out vec4 vBands;
flat out ivec4 vData;
const vec2 corners[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(0,1),vec2(0,1),vec2(1,0),vec2(1,1));
void main()
{
    vec2 corner=corners[gl_VertexID];
    float padding=aData.w==0?aAux.w:0.0;
    vec2 p=aRect.xy-vec2(padding)+corner*(aRect.zw+2.0*padding);
    gl_Position=vec4(p.x/uViewport.x*2.0-1.0,1.0-p.y/uViewport.y*2.0,0,1);
    vPosition=p;vEm=aAux.xy+corner*aAux.zw;
    vRect=aRect;vParams=aAux;vColor=aColor;vBorder=aBorder;
    vClip=aClip;vBands=aBands;vData=aData;
}
)GLSL";

// Adapted from Eric Lengyel's Slug reference pixel shader (MIT).
// Curve and band addresses use linear texture buffers instead of 2D textures.
// Axis-aligned glyph quads are expanded by a physical-pixel margin on the CPU.
const char* UnifiedFragment = R"GLSL(#version 330 core
in vec2 vEm;
in vec2 vPosition;
flat in vec4 vRect;
flat in vec4 vParams;
flat in vec4 vBorder;
flat in vec4 vColor;
flat in vec4 vClip;
flat in vec4 vBands;
flat in ivec4 vData;
uniform samplerBuffer uCurves;
uniform usamplerBuffer uBands;
uniform sampler2D uImage;
out vec4 result;
uint rootCode(float y1,float y2,float y3)
{
    uint i1=floatBitsToUint(y1)>>31u;
    uint i2=floatBitsToUint(y2)>>30u;
    uint i3=floatBitsToUint(y3)>>29u;
    uint shift=(i2&2u)|(i1&~2u);
    shift=(i3&4u)|(shift&~4u);
    return (0x2E74u>>shift)&0x0101u;
}
vec2 solveCurve(vec4 p12,vec2 p3,bool vertical)
{
    vec2 a=p12.xy-2.0*p12.zw+p3;
    vec2 b=p12.xy-p12.zw;
    float av=vertical?a.x:a.y;
    float bv=vertical?b.x:b.y;
    float cv=vertical?p12.x:p12.y;
    vec2 t;
    if(abs(av)<1.0/65536.0)
        t=vec2(abs(bv)>0.000000001?cv/(2.0*bv):0.0);
    else
    {
        float d=sqrt(max(bv*bv-av*cv,0.0));
        t=vec2(bv-d,bv+d)/av;
    }
    if(vertical) return (a.y*t-2.0*b.y)*t+p12.y;
    return (a.x*t-2.0*b.x)*t+p12.x;
}
void main()
{
    if(any(lessThan(vPosition,vClip.xy)) || any(greaterThanEqual(vPosition,vClip.xy+vClip.zw))) discard;
    if(vData.w==2)
    {
        result=texture(uImage,vEm)*vColor;
        return;
    }
    if(vData.w==0)
    {
        float radius=min(vParams.x,min(vRect.z,vRect.w)*0.5);
        vec2 q=abs(vPosition-vRect.xy-vRect.zw*0.5)-vRect.zw*0.5+radius;
        float d=length(max(q,vec2(0)))+min(max(q.x,q.y),0.0)-radius;
        float aa=max(fwidth(d),0.0001);
        float coverage=clamp(0.5-d/aa,0.0,1.0);
        vec4 color=vColor;
        if(vParams.z>0.0)
            coverage=1.0-smoothstep(-vParams.z,vParams.z,d);
        else if(vParams.y>0.0)
        {
            float interior=clamp(0.5-(d+vParams.y)/aa,0.0,1.0);
            color=mix(vBorder,vColor,interior);
        }
        result=vec4(color.rgb,color.a*coverage);
        return;
    }
    vec2 pixelsPerEm=1.0/max(fwidth(vEm),vec2(0.0000001));
    ivec2 index=clamp(ivec2(vEm*vBands.xy+vBands.zw),ivec2(0),vData.yz-ivec2(1));
    uvec2 h=texelFetch(uBands,vData.x+index.y).xy;
    float xcov=0.0,xwgt=0.0;
    for(uint i=0u;i<h.x;++i)
    {
        int location=int(texelFetch(uBands,int(h.y+i)).x);
        vec4 p12=texelFetch(uCurves,location)-vec4(vEm,vEm);
        vec2 p3=texelFetch(uCurves,location+1).xy-vEm;
        if(max(max(p12.x,p12.z),p3.x)*pixelsPerEm.x < -0.5) break;
        uint code=rootCode(p12.y,p12.w,p3.y);
        if(code!=0u)
        {
            vec2 r=solveCurve(p12,p3,false)*pixelsPerEm.x;
            if((code&1u)!=0u) { xcov+=clamp(r.x+0.5,0.0,1.0);xwgt=max(xwgt,clamp(1.0-abs(r.x)*2.0,0.0,1.0)); }
            if(code>1u) { xcov-=clamp(r.y+0.5,0.0,1.0);xwgt=max(xwgt,clamp(1.0-abs(r.y)*2.0,0.0,1.0)); }
        }
    }
    uvec2 v=texelFetch(uBands,vData.x+vData.z+index.x).xy;
    float ycov=0.0,ywgt=0.0;
    for(uint i=0u;i<v.x;++i)
    {
        int location=int(texelFetch(uBands,int(v.y+i)).x);
        vec4 p12=texelFetch(uCurves,location)-vec4(vEm,vEm);
        vec2 p3=texelFetch(uCurves,location+1).xy-vEm;
        if(max(max(p12.y,p12.w),p3.y)*pixelsPerEm.y < -0.5) break;
        uint code=rootCode(p12.x,p12.z,p3.x);
        if(code!=0u)
        {
            vec2 r=solveCurve(p12,p3,true)*pixelsPerEm.y;
            if((code&1u)!=0u) { ycov-=clamp(r.x+0.5,0.0,1.0);ywgt=max(ywgt,clamp(1.0-abs(r.x)*2.0,0.0,1.0)); }
            if(code>1u) { ycov+=clamp(r.y+0.5,0.0,1.0);ywgt=max(ywgt,clamp(1.0-abs(r.y)*2.0,0.0,1.0)); }
        }
    }
    float coverage=max(abs(xcov*xwgt+ycov*ywgt)/max(xwgt+ywgt,1.0/65536.0),min(abs(xcov),abs(ycov)));
    result=vec4(vColor.rgb,vColor.a*clamp(coverage,0.0,1.0));
}
)GLSL";

GLuint Compile(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char log[8192]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("GL shader compilation failed: ") + log);
    }
    return shader;
}

GLuint Program(const char* vertex, const char* fragment)
{
    const auto vs = Compile(GL_VERTEX_SHADER, vertex);
    GLuint fs = 0;
    GLuint program = 0;
    try
    {
        fs = Compile(GL_FRAGMENT_SHADER, fragment);
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        GLint success = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success)
        {
            char log[8192]{};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("GL program link failed: ") + log);
        }
    }
    catch (...)
    {
        glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        if (program)
            glDeleteProgram(program);
        throw;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

using Float4 = std::array<float, 4>;
using UInt2 = std::array<std::uint32_t, 2>;

Float4 Values(Rect rect)
{
    return {rect.m_X, rect.m_Y, rect.m_Width, rect.m_Height};
}
} // namespace

struct Renderer::Impl
{
    struct Instance
    {
        Float4 m_Rect;
        Float4 m_Aux;
        Color m_Color;
        Color m_Border;
        Float4 m_Clip;
        Float4 m_Bands;
        std::array<int, 4> m_Data;
    };

    struct GpuGlyph
    {
        GlyphGeometry m_Bounds;
        int m_Start = 0;
        int m_Count = 1;
        Float4 m_Transform{};
    };

    FontFace& m_Font;
    GLuint m_Program = 0;
    GLint m_ViewportLocation = -1;
    GLuint m_Vao = 0;
    GLuint m_InstanceBuffer = 0;
    GLuint m_CurveBuffer = 0;
    GLuint m_BandBuffer = 0;
    GLuint m_CurveTexture = 0;
    GLuint m_BandTexture = 0;
    float m_Width = 1;
    float m_Height = 1;
    int m_FramebufferWidth = 1;
    int m_FramebufferHeight = 1;
    bool m_FontDirty = true;
    std::vector<Instance> m_Instances;
    std::vector<std::pair<std::size_t, GLuint>> m_Images;
    std::size_t m_ShapeCount = 0;
    std::size_t m_GlyphCount = 0;
    std::vector<Float4> m_Curves{{0, 0, 0, 0}};
    std::vector<UInt2> m_Bands{{0, 0}};
    std::unordered_map<std::uint32_t, GpuGlyph> m_GlyphCache;
    RenderStats m_Stats;

    explicit Impl(FontFace& font) : m_Font(font)
    {
    }

    ~Impl()
    {
        if (m_Program)
            glDeleteProgram(m_Program);
        glDeleteBuffers(1, &m_InstanceBuffer);
        glDeleteBuffers(1, &m_CurveBuffer);
        glDeleteBuffers(1, &m_BandBuffer);
        glDeleteTextures(1, &m_CurveTexture);
        glDeleteTextures(1, &m_BandTexture);
        glDeleteVertexArrays(1, &m_Vao);
    }

    void Shape(Rect bounds, Color color, Color border, Float4 params, Rect clip)
    {
        if (bounds.m_Width <= 0 || bounds.m_Height <= 0 || clip.m_Width <= 0 || clip.m_Height <= 0)
            return;
        const float padding = params[3];
        const auto visible = Rect{bounds.m_X - padding, bounds.m_Y - padding, bounds.m_Width + 2 * padding,
                                  bounds.m_Height + 2 * padding}
                                 .Intersect(clip);
        if (visible.m_Width <= 0 || visible.m_Height <= 0)
            return;
        ++m_ShapeCount;
        m_Instances.push_back({Values(bounds), params, color, border, Values(clip), {}, {0, 0, 0, 0}});
    }

    const GpuGlyph& Glyph(std::uint32_t id)
    {
        if (const auto found = m_GlyphCache.find(id); found != m_GlyphCache.end())
            return found->second;
        const auto& geometry = m_Font.GetGlyph(id);
        GpuGlyph glyph;
        glyph.m_Bounds.m_MinX = geometry.m_MinX;
        glyph.m_Bounds.m_MinY = geometry.m_MinY;
        glyph.m_Bounds.m_MaxX = geometry.m_MaxX;
        glyph.m_Bounds.m_MaxY = geometry.m_MaxY;
        if (geometry.m_Curves.empty())
            return m_GlyphCache.emplace(id, std::move(glyph)).first->second;

        std::vector<std::uint32_t> locations;
        for (const auto& curve : geometry.m_Curves)
        {
            locations.push_back(static_cast<std::uint32_t>(m_Curves.size()));
            m_Curves.push_back({curve.m_P0.m_X, curve.m_P0.m_Y, curve.m_P1.m_X, curve.m_P1.m_Y});
            m_Curves.push_back({curve.m_P2.m_X, curve.m_P2.m_Y, 0, 0});
        }
        const int count = std::clamp(static_cast<int>(std::sqrt(geometry.m_Curves.size())), 1, 16);
        glyph.m_Start = static_cast<int>(m_Bands.size());
        glyph.m_Count = count;
        const float width = std::max(0.000001f, geometry.m_MaxX - geometry.m_MinX);
        const float height = std::max(0.000001f, geometry.m_MaxY - geometry.m_MinY);
        const float bandCount = static_cast<float>(count);
        glyph.m_Transform = {bandCount / width, bandCount / height, -geometry.m_MinX * bandCount / width,
                             -geometry.m_MinY * bandCount / height};
        m_Bands.resize(m_Bands.size() + static_cast<std::size_t>(2 * count));
        for (int direction = 0; direction < 2; ++direction)
        {
            for (int band = 0; band < count; ++band)
            {
                const float minimum = direction == 0 ? geometry.m_MinY : geometry.m_MinX;
                const float extent = direction == 0 ? height : width;
                const float lower = minimum + extent * static_cast<float>(band) / bandCount - 1.0f / 1024.0f;
                const float upper = minimum + extent * static_cast<float>(band + 1) / bandCount + 1.0f / 1024.0f;
                std::vector<std::size_t> candidates;
                const auto maxParallel = [&](std::size_t index)
                {
                    const auto& c = geometry.m_Curves[index];
                    return direction == 0 ? std::max({c.m_P0.m_X, c.m_P1.m_X, c.m_P2.m_X})
                                          : std::max({c.m_P0.m_Y, c.m_P1.m_Y, c.m_P2.m_Y});
                };
                for (std::size_t i = 0; i < geometry.m_Curves.size(); ++i)
                {
                    const auto& c = geometry.m_Curves[i];
                    const float a = direction == 0 ? c.m_P0.m_Y : c.m_P0.m_X;
                    const float b = direction == 0 ? c.m_P1.m_Y : c.m_P1.m_X;
                    const float d = direction == 0 ? c.m_P2.m_Y : c.m_P2.m_X;
                    if (std::max({a, b, d}) >= lower && std::min({a, b, d}) <= upper &&
                        std::max({a, b, d}) - std::min({a, b, d}) > 0.00000001f)
                        candidates.push_back(i);
                }
                std::sort(candidates.begin(), candidates.end(),
                          [&](auto a, auto b) { return maxParallel(a) > maxParallel(b); });
                m_Bands[static_cast<std::size_t>(glyph.m_Start + direction * count + band)] = {
                    static_cast<std::uint32_t>(candidates.size()), static_cast<std::uint32_t>(m_Bands.size())};
                for (auto index : candidates)
                    m_Bands.push_back({locations[index], 0});
            }
        }
        m_FontDirty = true;
        return m_GlyphCache.emplace(id, std::move(glyph)).first->second;
    }

    static void FloatAttribute(unsigned location, GLsizei stride, std::size_t offset)
    {
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offset));
        glVertexAttribDivisor(location, 1);
    }
};

Renderer::Renderer(FontFace& font) : m_Impl(std::make_unique<Impl>(font))
{
    static_assert(sizeof(Impl::Instance) == 112 && sizeof(int) == 4,
                  "The GL vertex attribute stream requires a packed 112-byte instance and 32-bit integers.");
    if (!GLAD_GL_VERSION_3_3)
        throw std::runtime_error("OpenGL 3.3 core is required.");
    m_Impl->m_Program = Program(UnifiedVertex, UnifiedFragment);
    m_Impl->m_ViewportLocation = glGetUniformLocation(m_Impl->m_Program, "uViewport");
    glUseProgram(m_Impl->m_Program);
    glUniform1i(glGetUniformLocation(m_Impl->m_Program, "uCurves"), 0);
    glUniform1i(glGetUniformLocation(m_Impl->m_Program, "uBands"), 1);
    glGenVertexArrays(1, &m_Impl->m_Vao);
    glGenBuffers(1, &m_Impl->m_InstanceBuffer);
    glGenBuffers(1, &m_Impl->m_CurveBuffer);
    glGenBuffers(1, &m_Impl->m_BandBuffer);
    glGenTextures(1, &m_Impl->m_CurveTexture);
    glGenTextures(1, &m_Impl->m_BandTexture);
}

Renderer::~Renderer() = default;

void Renderer::BeginFrame(float width, float height, int framebufferWidth, int framebufferHeight, Color clear)
{
    auto& self = *m_Impl;
    self.m_Width = std::max(1.0f, width);
    self.m_Height = std::max(1.0f, height);
    self.m_FramebufferWidth = std::max(1, framebufferWidth);
    self.m_FramebufferHeight = std::max(1, framebufferHeight);
    self.m_Instances.clear();
    self.m_Images.clear();
    self.m_ShapeCount = 0;
    self.m_GlyphCount = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, self.m_FramebufferWidth, self.m_FramebufferHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(clear.m_R, clear.m_G, clear.m_B, clear.m_A);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::DrawBox(Rect bounds, const Style& style, Rect clip, float opacity)
{
    auto fill = style.m_Background;
    auto border = style.m_BorderColor;
    fill.m_A *= opacity;
    border.m_A *= opacity;
    if (style.m_Shadow.m_Color.m_A > 0)
    {
        auto shadow = style.m_Shadow.m_Color;
        shadow.m_A *= opacity;
        auto shadowBounds = bounds;
        shadowBounds.m_X += style.m_Shadow.m_X;
        shadowBounds.m_Y += style.m_Shadow.m_Y;
        m_Impl->Shape(shadowBounds, shadow, {}, {style.m_Radius, 0, style.m_Shadow.m_Blur, style.m_Shadow.m_Blur + 1},
                      clip);
    }
    if (fill.m_A > 0 || (style.m_BorderWidth > 0 && border.m_A > 0))
        m_Impl->Shape(bounds, fill, border, {style.m_Radius, style.m_BorderWidth, 0, 1}, clip);
}

void Renderer::DrawSolid(Rect bounds, Color color, Rect clip)
{
    if (color.m_A > 0)
        m_Impl->Shape(bounds, color, {}, {0, 0, 0, 1}, clip);
}

void Renderer::DrawText(const TextLayout& layout, Point baseline, Color color, Rect clip)
{
    if (clip.m_Width <= 0 || clip.m_Height <= 0 || color.m_A <= 0 || layout.m_FontSize <= 0)
        return;
    auto& self = *m_Impl;
    const float paddingX = 0.75f * self.m_Width / static_cast<float>(self.m_FramebufferWidth);
    const float paddingY = 0.75f * self.m_Height / static_cast<float>(self.m_FramebufferHeight);
    const float size = layout.m_FontSize;
    for (const auto& positioned : layout.m_Glyphs)
    {
        const auto& glyph = self.Glyph(positioned.m_Id);
        const auto& bounds = glyph.m_Bounds;
        if (bounds.m_MaxX <= bounds.m_MinX || bounds.m_MaxY <= bounds.m_MinY)
            continue;
        const Rect rect{baseline.m_X + positioned.m_X + bounds.m_MinX * size - paddingX,
                        baseline.m_Y - positioned.m_Y - bounds.m_MaxY * size - paddingY,
                        (bounds.m_MaxX - bounds.m_MinX) * size + 2 * paddingX,
                        (bounds.m_MaxY - bounds.m_MinY) * size + 2 * paddingY};
        if (rect.Intersect(clip).m_Width <= 0 || rect.Intersect(clip).m_Height <= 0)
            continue;
        ++self.m_GlyphCount;
        self.m_Instances.push_back({Values(rect),
                                    {bounds.m_MinX - paddingX / size, bounds.m_MaxY + paddingY / size,
                                     rect.m_Width / size, -rect.m_Height / size},
                                    color,
                                    {},
                                    Values(clip),
                                    glyph.m_Transform,
                                    {glyph.m_Start, glyph.m_Count, glyph.m_Count, 1}});
    }
}

void Renderer::DrawTexture(Rect bounds, std::uint32_t texture, Rect clip, float opacity)
{
    if (!texture || bounds.m_Width <= 0 || bounds.m_Height <= 0 || bounds.Intersect(clip).m_Width <= 0 ||
        bounds.Intersect(clip).m_Height <= 0)
        return;
    auto& self = *m_Impl;
    self.m_Images.emplace_back(self.m_Instances.size(), texture);
    self.m_Instances.push_back({Values(bounds), {0, 1, 1, -1}, {1, 1, 1, opacity}, {}, Values(clip), {}, {0, 0, 0, 2}});
}

void Renderer::EndFrame()
{
    auto& self = *m_Impl;
    glBindVertexArray(self.m_Vao);
    if (self.m_FontDirty)
    {
        GLint maximum = 0;
        glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maximum);
        if (self.m_Curves.size() > static_cast<std::size_t>(maximum) ||
            self.m_Bands.size() > static_cast<std::size_t>(maximum))
            throw std::runtime_error("Glyph cache exceeds the driver's texture buffer limit.");
        glBindBuffer(GL_TEXTURE_BUFFER, self.m_CurveBuffer);
        glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(self.m_Curves.size() * sizeof(Float4)),
                     self.m_Curves.data(), GL_DYNAMIC_DRAW);
        glBindTexture(GL_TEXTURE_BUFFER, self.m_CurveTexture);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, self.m_CurveBuffer);
        glBindBuffer(GL_TEXTURE_BUFFER, self.m_BandBuffer);
        glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(self.m_Bands.size() * sizeof(UInt2)),
                     self.m_Bands.data(), GL_DYNAMIC_DRAW);
        glBindTexture(GL_TEXTURE_BUFFER, self.m_BandTexture);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32UI, self.m_BandBuffer);
        self.m_FontDirty = false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, self.m_InstanceBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(self.m_Instances.size() * sizeof(Impl::Instance)),
                 self.m_Instances.data(), GL_STREAM_DRAW);
    glUseProgram(self.m_Program);
    glUniform2f(self.m_ViewportLocation, self.m_Width, self.m_Height);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, self.m_CurveTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, self.m_BandTexture);
    glUniform1i(glGetUniformLocation(self.m_Program, "uImage"), 2);
    if (self.m_Instances.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
        throw std::runtime_error("Frame exceeds the maximum GL instance count.");
    std::size_t calls = 0;
    const auto submit = [&](std::size_t begin, std::size_t end)
    {
        if (end <= begin)
            return;
        const auto offset = begin * sizeof(Impl::Instance);
        for (unsigned i = 0; i < 6; ++i)
            Impl::FloatAttribute(i, sizeof(Impl::Instance), offset + i * sizeof(Float4));
        glEnableVertexAttribArray(6);
        glVertexAttribIPointer(6, 4, GL_INT, sizeof(Impl::Instance),
                               reinterpret_cast<const void*>(offset + offsetof(Impl::Instance, m_Data)));
        glVertexAttribDivisor(6, 1);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(end - begin));
        ++calls;
    };
    std::size_t begin = 0;
    for (const auto& image : self.m_Images)
    {
        submit(begin, image.first);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, image.second);
        begin = image.first;
    }
    submit(begin, self.m_Instances.size());
    self.m_Stats = {calls, self.m_ShapeCount, self.m_GlyphCount, self.m_GlyphCache.size(),
                    self.m_Curves.size() * sizeof(Float4) + self.m_Bands.size() * sizeof(UInt2)};
    const auto error = glGetError();
    if (error != GL_NO_ERROR)
        throw std::runtime_error("OpenGL error after rendering: " + std::to_string(error));
}

Color Renderer::ReadPixel(Point point) const
{
    const auto& self = *m_Impl;
    const int x = std::clamp(static_cast<int>(point.m_X * static_cast<float>(self.m_FramebufferWidth) / self.m_Width),
                             0, self.m_FramebufferWidth - 1);
    const int y =
        std::clamp(self.m_FramebufferHeight - 1 -
                       static_cast<int>(point.m_Y * static_cast<float>(self.m_FramebufferHeight) / self.m_Height),
                   0, self.m_FramebufferHeight - 1);
    unsigned char pixel[4]{};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return {pixel[0] / 255.0f, pixel[1] / 255.0f, pixel[2] / 255.0f, pixel[3] / 255.0f};
}

void Renderer::SavePpm(const std::filesystem::path& path) const
{
    const auto& self = *m_Impl;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(self.m_FramebufferWidth) *
                                      static_cast<std::size_t>(self.m_FramebufferHeight) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, self.m_FramebufferWidth, self.m_FramebufferHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream output(path, std::ios::binary);
    if (!output)
        throw std::runtime_error("Cannot write screenshot: " + path.string());
    output << "P6\n" << self.m_FramebufferWidth << ' ' << self.m_FramebufferHeight << "\n255\n";
    const auto stride = static_cast<std::size_t>(self.m_FramebufferWidth) * 3;
    for (int y = self.m_FramebufferHeight - 1; y >= 0; --y)
        output.write(reinterpret_cast<const char*>(pixels.data() + static_cast<std::size_t>(y) * stride),
                     static_cast<std::streamsize>(stride));
    if (!output)
        throw std::runtime_error("Screenshot write failed.");
}

const RenderStats& Renderer::GetStats() const
{
    return m_Impl->m_Stats;
}

std::string Renderer::GetDriverDescription() const
{
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    return std::string(renderer ? renderer : "unknown") + " | " + (version ? version : "unknown");
}
} // namespace EStudioUI
