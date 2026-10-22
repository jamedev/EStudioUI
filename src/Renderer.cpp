#include <EStudioUI/Renderer.h>

#include <exception>
#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
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
layout(location=7) in ivec4 aEffects;
uniform samplerBuffer uEffects;
uniform vec2 uViewport;
out vec2 vEm;
out vec2 vPosition;
out vec2 vWorld;
flat out ivec4 vEffects;
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
    float padding=(aData.w==0 || aData.w==4)?aAux.w:0.0;
    vec2 p=aRect.xy-vec2(padding)+corner*(aRect.zw+2.0*padding);
    if(aData.w==5)
    {
        vec2 segment=aAux.zw-aAux.xy;
        float extent=length(segment);
        vec2 direction=extent>0.0?segment/extent:vec2(1,0);
        vec2 normal=vec2(-direction.y,direction.x);
        float margin=aBands.x+aBands.y;
        p=aAux.xy+direction*(-margin+corner.x*(extent+2.0*margin))
            +normal*((corner.y*2.0-1.0)*margin);
    }
    vec2 world=p;
    if(aEffects.x!=0)
    {
        vec4 row0=texelFetch(uEffects,aEffects.x);
        vec4 row1=texelFetch(uEffects,aEffects.x+1);
        world=vec2(dot(vec3(p,1),row0.xyz),dot(vec3(p,1),row1.xyz));
    }
    gl_Position=vec4(world.x/uViewport.x*2.0-1.0,1.0-world.y/uViewport.y*2.0,0,1);
    vWorld=world;vEffects=aEffects;
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
in vec2 vWorld;
flat in ivec4 vEffects;
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
uniform samplerBuffer uEffects;
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
float boxDistance(vec2 point, vec4 rect, float radius)
{
    radius=clamp(radius,0.0,min(rect.z,rect.w)*0.5);
    vec2 q=abs(point-(rect.xy+rect.zw*0.5))-(rect.zw*0.5-vec2(radius));
    return (q.x>0.0 && q.y>0.0 ? length(q) : max(q.x,q.y))-radius;
}
float coverage(float distance)
{
    float aa=max(fwidth(distance),0.0001);
    return 1.0-smoothstep(-aa*0.5,aa*0.5,distance);
}
vec4 premultiply(vec4 color) { return vec4(color.rgb*color.a,color.a); }
vec4 straight(vec4 color) { return vec4(color.a>0.000001?color.rgb/color.a:vec3(0),color.a); }
vec4 gradientColor(int index)
{
    vec4 header=texelFetch(uEffects,index);
    float t;
    if(header.x==1.0)
        t=0.5+dot(vPosition-vRect.xy-vRect.zw*0.5,header.zw);
    else
    {
        vec4 params=texelFetch(uEffects,index+1);
        t=length((vPosition-params.xy)*params.zw);
    }
    float previousPosition=texelFetch(uEffects,index+2).x;
    vec4 previousColor=texelFetch(uEffects,index+3);
    if(t<previousPosition) return previousColor;
    for(int i=1;i<8;++i)
    {
        if(i>=int(header.y)) break;
        float position=texelFetch(uEffects,index+2+i*2).x;
        vec4 color=texelFetch(uEffects,index+3+i*2);
        if(t<position)
        {
            float weight=clamp((t-previousPosition)/max(position-previousPosition,0.000001),0.0,1.0);
            return mix(previousColor,color,weight);
        }
        previousPosition=position;previousColor=color;
    }
    return previousColor;
}
void main()
{
    if(any(lessThan(vWorld,vClip.xy)) || any(greaterThanEqual(vWorld,vClip.xy+vClip.zw))) discard;
    int clip=vEffects.y;
    float clipCoverage=1.0;
    for(int depth=0;depth<32;++depth)
    {
        if(clip==0) break;
        vec4 rect=texelFetch(uEffects,clip);
        vec4 first=texelFetch(uEffects,clip+1);
        vec4 second=texelFetch(uEffects,clip+2);
        vec2 local=vec2(dot(vec3(vWorld,1),vec3(first.zw,second.x)),dot(vec3(vWorld,1),second.yzw));
        clipCoverage=min(clipCoverage,coverage(boxDistance(local,rect,first.x)));
        if(clipCoverage<=0.0) discard;
        clip=int(first.y);
    }
    if(vData.w==2)
    {
        result=texture(uImage,vEm)*vColor;
        result.a*=clipCoverage;
        return;
    }
    if(vData.w==5)
    {
        vec2 segment=vParams.zw-vParams.xy;
        vec2 offset=vPosition-vParams.xy;
        float squaredLength=dot(segment,segment);
        float t=squaredLength>0.0?clamp(dot(offset,segment)/squaredLength,0.0,1.0):0.0;
        float d=length(offset-segment*t)-vBands.x;
        result=vec4(vColor.rgb,vColor.a*coverage(d)*clipCoverage);
        return;
    }
    if(vData.w==0 || vData.w==3 || vData.w==4)
    {
        float d=boxDistance(vPosition,vRect,vParams.x);
        float alpha=coverage(d);
        if(vParams.z>0.0) alpha=1.0-smoothstep(-vParams.z,vParams.z,d);
        vec4 fill=vColor;
        if(vEffects.z!=0)
        {
            vec4 image=gradientColor(vEffects.z);
            fill=straight(image+premultiply(fill)*(1.0-image.a));
        }
        if(vData.w==3)
        {
            vec4 hole=texelFetch(uEffects,vEffects.w);
            vec4 shadow=texelFetch(uEffects,vEffects.w+1);
            float holeDistance=boxDistance(vPosition,hole,shadow.x);
            float holeCoverage=shadow.y>0.0?1.0-smoothstep(-shadow.y,shadow.y,holeDistance):coverage(holeDistance);
            alpha=coverage(d)*(1.0-holeCoverage);
        }
        if(vData.w==4)
        {
            vec4 owner=texelFetch(uEffects,vEffects.w);
            float radius=texelFetch(uEffects,vEffects.w+1).x;
            alpha*=1.0-coverage(boxDistance(vPosition,owner,radius));
        }
        if(vParams.y>0.0)
        {
            float inner=coverage(d+vParams.y);
            fill=straight(mix(premultiply(vBorder),premultiply(fill),inner));
        }
        result=vec4(fill.rgb,fill.a*alpha*clipCoverage);
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
    result=vec4(vColor.rgb,vColor.a*clamp(coverage,0.0,1.0)*clipCoverage);
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

// Captures use the renderer's GPU image, not a native buffer whose contents may
// be discarded by a swap. Preserve external readback state, including pack PBOs.
class FrameReadScope
{
  public:
    explicit FrameReadScope(GLuint framebuffer)
    {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_Framebuffer);
        glGetIntegerv(GL_READ_BUFFER, &m_ReadBuffer);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &m_PackBuffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &m_Alignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &m_RowLength);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &m_SkipRows);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &m_SkipPixels);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    }

    ~FrameReadScope()
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(m_Framebuffer));
        glReadBuffer(static_cast<GLenum>(m_ReadBuffer));
        glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(m_PackBuffer));
        glPixelStorei(GL_PACK_ALIGNMENT, m_Alignment);
        glPixelStorei(GL_PACK_ROW_LENGTH, m_RowLength);
        glPixelStorei(GL_PACK_SKIP_ROWS, m_SkipRows);
        glPixelStorei(GL_PACK_SKIP_PIXELS, m_SkipPixels);
    }

    FrameReadScope(const FrameReadScope&) = delete;
    FrameReadScope& operator=(const FrameReadScope&) = delete;

  private:
    GLint m_Framebuffer = 0, m_ReadBuffer = 0, m_PackBuffer = 0;
    GLint m_Alignment = 0, m_RowLength = 0, m_SkipRows = 0, m_SkipPixels = 0;
};
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
        std::array<int, 4> m_Effects;
    };

    struct GpuGlyph
    {
        GlyphGeometry m_Bounds;
        int m_Start = 0;
        int m_Count = 1;
        Float4 m_Transform{};
    };

    struct State
    {
        Affine m_Transform;
        int m_TransformIndex = 0, m_ClipIndex = 0, m_ClipDepth = 0;
        Rect m_Scissor, m_IgnoredClip, m_Owner;
        const Style* m_Before = nullptr;
        bool m_BeforeDrawn = false, m_Rounded = false;
        float m_Radius = 0;
        std::uintptr_t m_OwnerIdentity = 0;
        std::uint64_t m_SubIdentity = 0;
        bool m_CustomIdentity = false;
        unsigned m_Layer = 0;
    };

    struct PaintOwner
    {
        std::uintptr_t m_Owner;
        std::uint64_t m_SubIdentity;
        bool m_Custom;
        bool operator==(const PaintOwner&) const = default;
    };

    struct PaintOwnerHash
    {
        std::size_t operator()(const PaintOwner& owner) const
        {
            return std::hash<std::uintptr_t>{}(owner.m_Owner) ^
                   (std::hash<std::uint64_t>{}(owner.m_SubIdentity) * 0x9e3779b9u) ^
                   (static_cast<std::size_t>(owner.m_Custom) * 0x85ebca6bu);
        }
    };

    struct PaintKey
    {
        PaintOwner m_Owner;
        std::uint64_t m_Sequence;
        bool operator==(const PaintKey&) const = default;
    };

    struct PaintKeyHash
    {
        std::size_t operator()(const PaintKey& key) const
        {
            return PaintOwnerHash{}(key.m_Owner) ^ (std::hash<std::uint64_t>{}(key.m_Sequence) * 0x9e3779b9u);
        }
    };

    std::vector<PaintKey> m_PaintKeys, m_PreviousKeys;
    static constexpr std::size_t s_PrimitiveKinds = 6;
    std::unordered_map<PaintOwner, std::array<unsigned, 3 * s_PrimitiveKinds>, PaintOwnerHash> m_OwnerCounters;

    PaintKey NextPaintKey(std::size_t kind)
    {
        const auto& state = m_States.back();
        const auto slot = static_cast<std::size_t>(state.m_Layer) * s_PrimitiveKinds + kind;
        const PaintOwner owner{state.m_OwnerIdentity, state.m_SubIdentity, state.m_CustomIdentity};
        auto& sequence = m_OwnerCounters[owner][slot];
        return {owner, (static_cast<std::uint64_t>(sequence++) << 8) | slot};
    }

    void Record(Instance instance)
    {
        m_PaintKeys.push_back(NextPaintKey(static_cast<std::size_t>(instance.m_Data[3])));
        m_Instances.push_back(instance);
    }

    struct Image
    {
        std::size_t m_Instance;
        GLuint m_Texture;
        std::uint64_t m_Revision;
    };

    struct TextureState
    {
        GLuint m_Texture = 0;
        std::uint64_t m_Revision = 0;
        bool operator==(const TextureState&) const = default;
    };

    struct PixelRect
    {
        int m_X, m_Y, m_Width, m_Height;
        bool operator==(const PixelRect&) const = default;
    };

    struct Batch
    {
        PixelRect m_Damage;
        std::size_t m_Begin, m_End;
        GLuint m_Texture;
    };

    bool m_PartialRedraw = true, m_HistoryValid = false;
    bool m_FrameReady = false;
    GLuint m_RetainedFramebuffer = 0, m_RetainedTexture = 0;
    int m_RetainedWidth = 0, m_RetainedHeight = 0;
    float m_PreviousWidth = 0, m_PreviousHeight = 0;
    Color m_Clear, m_PreviousClear;
    std::vector<Instance> m_PreviousInstances, m_Submitted;
    std::vector<Float4> m_PreviousEffects;
    std::vector<TextureState> m_TextureStates, m_PreviousTextures;
    std::vector<Rect> m_Bounds, m_PreviousBounds;
    std::vector<unsigned char> m_DirtyTiles;
    std::vector<PixelRect> m_Damage;
    std::vector<Batch> m_Batches;
    std::size_t m_RepaintPixels = 0, m_ReplayedGlyphs = 0;

    bool m_TimingEnabled = false;
    std::array<GLuint, 8> m_Queries{};
    std::array<bool, 8> m_QueryPending{};
    std::array<std::uint64_t, 8> m_QuerySerials{};
    std::uint64_t m_NextQuerySerial = 0, m_LastQuerySerial = 0;
    int m_ActiveQuery = -1;
    double m_GpuMilliseconds = -1;
    std::uint64_t m_TimingSamples = 0;

    void PollTimers()
    {
        if (!m_TimingEnabled)
            return;
        for (std::size_t i = 0; i < m_Queries.size(); ++i)
        {
            if (!m_QueryPending[i])
                continue;
            GLint available = 0;
            glGetQueryObjectiv(m_Queries[i], GL_QUERY_RESULT_AVAILABLE, &available);
            if (!available)
                continue;
            GLuint64 nanoseconds = 0;
            glGetQueryObjectui64v(m_Queries[i], GL_QUERY_RESULT, &nanoseconds);
            if (m_QuerySerials[i] > m_LastQuerySerial)
            {
                m_GpuMilliseconds = static_cast<double>(nanoseconds) / 1000000;
                m_LastQuerySerial = m_QuerySerials[i];
            }
            ++m_TimingSamples;
            m_QueryPending[i] = false;
        }
    }

    void StartTimer()
    {
        if (!m_TimingEnabled)
            return;
        for (std::size_t i = 0; i < m_Queries.size(); ++i)
            if (!m_QueryPending[i])
            {
                glBeginQuery(GL_TIME_ELAPSED, m_Queries[i]);
                m_QuerySerials[i] = ++m_NextQuerySerial;
                m_ActiveQuery = static_cast<int>(i);
                return;
            }
    }

    void EnsureRetainedTarget()
    {
        if (m_RetainedWidth == m_FramebufferWidth && m_RetainedHeight == m_FramebufferHeight)
            return;
        if (m_FramebufferWidth > m_MaxTextureSize || m_FramebufferHeight > m_MaxTextureSize)
            throw std::runtime_error("UI framebuffer exceeds the texture size limit.");
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, m_RetainedTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_FramebufferWidth, m_FramebufferHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RetainedFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_RetainedTexture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Retained UI framebuffer is incomplete.");
        m_RetainedWidth = m_FramebufferWidth;
        m_RetainedHeight = m_FramebufferHeight;
        m_HistoryValid = false;
    }

    static Rect Box(const Float4& value)
    {
        return {value[0], value[1], value[2], value[3]};
    }

    Rect InstanceBounds(const Instance& instance) const
    {
        Rect bounds = Box(instance.m_Rect);
        const float padding = instance.m_Data[3] == 0 || instance.m_Data[3] == 4 ? instance.m_Aux[3] : 0;
        bounds = {bounds.m_X - padding, bounds.m_Y - padding, bounds.m_Width + 2 * padding,
                  bounds.m_Height + 2 * padding};
        const auto index = static_cast<std::size_t>(instance.m_Effects[0]);
        const auto& x = m_Effects[index];
        const auto& y = m_Effects[index + 1];
        bounds = Affine{x[0], y[0], x[1], y[1], x[2], y[2]}.MapBounds(bounds).Intersect(Box(instance.m_Clip));
        for (int clip = instance.m_Effects[1]; clip;)
        {
            const auto& first = m_Effects[static_cast<std::size_t>(clip + 1)];
            const auto& second = m_Effects[static_cast<std::size_t>(clip + 2)];
            const Affine inverse{first[2], second[1], first[3], second[2], second[0], second[3]};
            Affine matrix;
            if (!inverse.Inverse(matrix))
                return {};
            auto clipBounds = matrix.MapBounds(Box(m_Effects[static_cast<std::size_t>(clip)]));
            // Derivative-based clip coverage extends beyond its mathematical edge.
            const float px = m_Width / static_cast<float>(m_FramebufferWidth);
            const float py = m_Height / static_cast<float>(m_FramebufferHeight);
            clipBounds = {clipBounds.m_X - px, clipBounds.m_Y - py, clipBounds.m_Width + 2 * px,
                          clipBounds.m_Height + 2 * py};
            bounds = bounds.Intersect(clipBounds);
            clip = static_cast<int>(first[1]);
        }
        return bounds;
    }

    bool SamePaint(std::size_t current, std::size_t previous) const
    {
        const auto& a = m_Instances[current];
        const auto& b = m_PreviousInstances[previous];
        if (std::memcmp(&a, &b, offsetof(Instance, m_Effects)) != 0)
            return false;
        if (m_TextureStates[current] != m_PreviousTextures[previous] ||
            (a.m_Data[3] == 2 && m_TextureStates[current].m_Revision == 0))
            return false;
        const auto sameRange = [&](int first, int last, std::size_t count)
        { return std::memcmp(m_Effects.data() + first, m_PreviousEffects.data() + last, count * sizeof(Float4)) == 0; };
        if (!sameRange(a.m_Effects[0], b.m_Effects[0], 2))
            return false;
        int first = a.m_Effects[1], last = b.m_Effects[1];
        while (first || last)
        {
            if (!first || !last || !sameRange(first, last, 1))
                return false;
            const auto& x = m_Effects[static_cast<std::size_t>(first + 1)];
            const auto& y = m_PreviousEffects[static_cast<std::size_t>(last + 1)];
            if (x[0] != y[0] || x[2] != y[2] || x[3] != y[3] || !sameRange(first + 2, last + 2, 1))
                return false;
            first = static_cast<int>(x[1]);
            last = static_cast<int>(y[1]);
        }
        first = a.m_Effects[2];
        last = b.m_Effects[2];
        if ((first == 0) != (last == 0))
            return false;
        if (first)
        {
            const auto count = static_cast<std::size_t>(m_Effects[static_cast<std::size_t>(first)][1]);
            if (!sameRange(first, last, 2) || !sameRange(first + 2, last + 2, count * 2))
                return false;
        }
        first = a.m_Effects[3];
        last = b.m_Effects[3];
        return (first == 0 && last == 0) || (first && last && sameRange(first, last, 2));
    }

    void BuildDamage()
    {
        constexpr int tile = 64;
        const int columns = (m_FramebufferWidth + tile - 1) / tile;
        const int rows = (m_FramebufferHeight + tile - 1) / tile;
        m_DirtyTiles.assign(static_cast<std::size_t>(columns * rows), 0);
        m_Damage.clear();
        m_Bounds.resize(m_Instances.size());
        m_TextureStates.assign(m_Instances.size(), {});
        for (const auto& image : m_Images)
            m_TextureStates[image.m_Instance] = {image.m_Texture, image.m_Revision};
        for (std::size_t i = 0; i < m_Instances.size(); ++i)
            m_Bounds[i] = InstanceBounds(m_Instances[i]);
        const auto full = [&]
        {
            m_Damage = {{0, 0, m_FramebufferWidth, m_FramebufferHeight}};
            m_RepaintPixels =
                static_cast<std::size_t>(m_FramebufferWidth) * static_cast<std::size_t>(m_FramebufferHeight);
        };
        if (!m_PartialRedraw || !m_HistoryValid || m_Clear != m_PreviousClear || m_Width != m_PreviousWidth ||
            m_Height != m_PreviousHeight)
        {
            full();
            return;
        }
        const auto dirty = [&](Rect bounds)
        {
            if (bounds.m_Width <= 0 || bounds.m_Height <= 0)
                return;
            const float sx = static_cast<float>(m_FramebufferWidth) / m_Width;
            const float sy = static_cast<float>(m_FramebufferHeight) / m_Height;
            const int left = static_cast<int>(
                std::clamp(std::floor(bounds.m_X * sx) - 1, 0.0f, static_cast<float>(m_FramebufferWidth)));
            const int top = static_cast<int>(
                std::clamp(std::floor(bounds.m_Y * sy) - 1, 0.0f, static_cast<float>(m_FramebufferHeight)));
            const int right = static_cast<int>(std::clamp(std::ceil((bounds.m_X + bounds.m_Width) * sx) + 1, 0.0f,
                                                          static_cast<float>(m_FramebufferWidth)));
            const int bottom = static_cast<int>(std::clamp(std::ceil((bounds.m_Y + bounds.m_Height) * sy) + 1, 0.0f,
                                                           static_cast<float>(m_FramebufferHeight)));
            for (int y = top / tile; y < (bottom + tile - 1) / tile; ++y)
                for (int x = left / tile; x < (right + tile - 1) / tile; ++x)
                    m_DirtyTiles[static_cast<std::size_t>(y * columns + x)] = 1;
        };
        const auto currentCount = m_Instances.size(), oldCount = m_PreviousInstances.size();
        std::size_t prefix = 0, suffix = 0;
        while (prefix < std::min(currentCount, oldCount) && SamePaint(prefix, prefix))
            ++prefix;
        while (suffix < std::min(currentCount, oldCount) - prefix &&
               SamePaint(currentCount - 1 - suffix, oldCount - 1 - suffix))
            ++suffix;
        if (currentCount == oldCount && m_PaintKeys == m_PreviousKeys)
        {
            for (std::size_t i = prefix; i < currentCount - suffix; ++i)
                if (!SamePaint(i, i))
                {
                    dirty(m_Bounds[i]);
                    dirty(m_PreviousBounds[i]);
                }
        }
        else
        {
            std::unordered_map<PaintKey, std::size_t, PaintKeyHash> previous;
            previous.reserve(oldCount);
            for (std::size_t i = 0; i < oldCount; ++i)
                previous.emplace(m_PreviousKeys[i], i);
            std::vector<bool> matched(oldCount, false);
            std::vector<std::size_t> mapping(currentCount, oldCount);
            for (std::size_t i = 0; i < currentCount; ++i)
            {
                const auto found = previous.find(m_PaintKeys[i]);
                if (found == previous.end())
                {
                    dirty(m_Bounds[i]);
                    continue;
                }
                const auto old = found->second;
                mapping[i] = old;
                matched[old] = true;
                if (!SamePaint(i, old))
                {
                    dirty(m_Bounds[i]);
                    dirty(m_PreviousBounds[old]);
                }
            }
            for (std::size_t i = 0; i < oldCount; ++i)
                if (!matched[i])
                    dirty(m_PreviousBounds[i]);
            // Every inversion participant can change overlap order. Insertions
            // merely shift indices and do not invalidate unrelated owners.
            std::size_t maximum = 0;
            for (std::size_t i = 0; i < currentCount; ++i)
            {
                const auto old = mapping[i];
                if (old == oldCount)
                    continue;
                if (old < maximum)
                {
                    dirty(m_Bounds[i]);
                    dirty(m_PreviousBounds[old]);
                }
                maximum = std::max(maximum, old);
            }
            std::size_t minimum = oldCount;
            for (std::size_t i = currentCount; i-- > 0;)
            {
                const auto old = mapping[i];
                if (old == oldCount)
                    continue;
                if (old > minimum)
                {
                    dirty(m_Bounds[i]);
                    dirty(m_PreviousBounds[old]);
                }
                minimum = std::min(minimum, old);
            }
        }
        m_RepaintPixels = 0;
        for (int y = 0; y < rows; ++y)
        {
            for (int x = 0; x < columns;)
            {
                if (!m_DirtyTiles[static_cast<std::size_t>(y * columns + x)])
                {
                    ++x;
                    continue;
                }
                const int start = x;
                while (x < columns && m_DirtyTiles[static_cast<std::size_t>(y * columns + x)])
                    ++x;
                PixelRect rect{start * tile, y * tile, std::min(x * tile, m_FramebufferWidth) - start * tile,
                               std::min(tile, m_FramebufferHeight - y * tile)};
                auto previous = std::find_if(m_Damage.begin(), m_Damage.end(),
                                             [&](const auto& item)
                                             {
                                                 return item.m_X == rect.m_X && item.m_Width == rect.m_Width &&
                                                        item.m_Y + item.m_Height == rect.m_Y;
                                             });
                if (previous != m_Damage.end())
                    previous->m_Height += rect.m_Height;
                else
                    m_Damage.push_back(rect);
                m_RepaintPixels += static_cast<std::size_t>(rect.m_Width * rect.m_Height);
                const auto pixels =
                    static_cast<std::size_t>(m_FramebufferWidth) * static_cast<std::size_t>(m_FramebufferHeight);
                if (m_Damage.size() > 16 || m_RepaintPixels > pixels * 3 / 5)
                {
                    full();
                    return;
                }
            }
        }
    }

    void BuildBatches()
    {
        m_Submitted.clear();
        m_Batches.clear();
        m_ReplayedGlyphs = 0;
        const float sx = m_Width / static_cast<float>(m_FramebufferWidth);
        const float sy = m_Height / static_cast<float>(m_FramebufferHeight);
        for (const auto& damage : m_Damage)
        {
            const bool full = damage.m_X == 0 && damage.m_Y == 0 && damage.m_Width == m_FramebufferWidth &&
                              damage.m_Height == m_FramebufferHeight;
            const Rect region{static_cast<float>(damage.m_X) * sx, static_cast<float>(damage.m_Y) * sy,
                              static_cast<float>(damage.m_Width) * sx, static_cast<float>(damage.m_Height) * sy};
            std::size_t begin = m_Submitted.size();
            GLuint texture = 0;
            for (std::size_t i = 0; i < m_Instances.size(); ++i)
            {
                const auto visible = m_Bounds[i].Intersect(region);
                if (!full && (visible.m_Width <= 0 || visible.m_Height <= 0))
                    continue;
                const auto image = m_TextureStates[i].m_Texture;
                if (image && image != texture)
                {
                    if (m_Submitted.size() > begin)
                        m_Batches.push_back({damage, begin, m_Submitted.size(), texture});
                    begin = m_Submitted.size();
                    texture = image;
                }
                m_Submitted.push_back(m_Instances[i]);
                if (m_Instances[i].m_Data[3] == 1)
                    ++m_ReplayedGlyphs;
            }
            if (m_Submitted.size() > begin)
                m_Batches.push_back({damage, begin, m_Submitted.size(), texture});
        }
    }

    GLuint m_EffectBuffer = 0, m_EffectTexture = 0;
    std::vector<Float4> m_Effects;
    std::vector<State> m_States;
    std::size_t m_GradientCount = 0, m_ShadowCount = 0;

    int AppendClip(Rect bounds, float radius, const State& state)
    {
        if (state.m_ClipDepth >= 32)
            throw std::runtime_error("Rounded/transformed clip nesting exceeds 32 levels.");
        Affine inverse;
        if (!state.m_Transform.Inverse(inverse))
            return -1;
        const auto index = static_cast<int>(m_Effects.size());
        m_Effects.push_back(Values(bounds));
        m_Effects.push_back({radius, static_cast<float>(state.m_ClipIndex), inverse.m_A, inverse.m_C});
        m_Effects.push_back({inverse.m_X, inverse.m_B, inverse.m_D, inverse.m_Y});
        return index;
    }

    std::pair<Rect, std::array<int, 4>> DrawingState(Rect clip)
    {
        auto state = m_States.back();
        if (clip.m_Width <= 0 || clip.m_Height <= 0)
            return {Rect{}, {0, -1, 0, 0}};
        if (clip != state.m_IgnoredClip)
        {
            if (state.m_Transform.m_B == 0 && state.m_Transform.m_C == 0)
                state.m_Scissor = state.m_Scissor.Intersect(state.m_Transform.MapBounds(clip));
            else
            {
                state.m_ClipIndex = AppendClip(clip, 0, state);
                ++state.m_ClipDepth;
            }
        }
        if (state.m_Rounded)
        {
            state.m_ClipIndex = AppendClip(state.m_Owner, state.m_Radius, state);
            ++state.m_ClipDepth;
        }
        return {state.m_Scissor, {state.m_TransformIndex, state.m_ClipIndex, 0, 0}};
    }

    int AddGradient(const Gradient& gradient, float opacity, Rect bounds)
    {
        if (gradient.m_Kind == Gradient::Kind::None)
            return 0;
        const int index = static_cast<int>(m_Effects.size());
        const float angle = gradient.m_Angle * 3.14159265358979323846f / 180;
        const float dx = std::sin(angle), dy = -std::cos(angle);
        const float extent = std::max(0.0001f, std::abs(dx) * bounds.m_Width + std::abs(dy) * bounds.m_Height);
        m_Effects.push_back({static_cast<float>(gradient.m_Kind), static_cast<float>(gradient.m_Stops.size()),
                             dx / extent, dy / extent});
        m_Effects.push_back({bounds.m_X + gradient.m_Center.m_X * bounds.m_Width,
                             bounds.m_Y + gradient.m_Center.m_Y * bounds.m_Height,
                             1 / std::max(0.0001f, gradient.m_Radius.m_X * bounds.m_Width),
                             1 / std::max(0.0001f, gradient.m_Radius.m_Y * bounds.m_Height)});
        for (const auto& stop : gradient.m_Stops)
        {
            m_Effects.push_back({stop.m_Position, 0, 0, 0});
            const float alpha = stop.m_Color.m_A * opacity;
            m_Effects.push_back({stop.m_Color.m_R * alpha, stop.m_Color.m_G * alpha, stop.m_Color.m_B * alpha, alpha});
        }
        ++m_GradientCount;
        return index;
    }

    bool Visible(Rect bounds, Rect clip) const
    {
        const auto visible = m_States.back().m_Transform.MapBounds(bounds).Intersect(clip);
        return visible.m_Width > 0 && visible.m_Height > 0;
    }

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
    std::vector<Image> m_Images;
    GLint m_MaxTextureSize = 0, m_MaxBufferSize = 0;
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
        glDeleteFramebuffers(1, &m_RetainedFramebuffer);
        glDeleteTextures(1, &m_RetainedTexture);
        glDeleteQueries(static_cast<GLsizei>(m_Queries.size()), m_Queries.data());
        glDeleteBuffers(1, &m_EffectBuffer);
        glDeleteTextures(1, &m_EffectTexture);
        glDeleteBuffers(1, &m_InstanceBuffer);
        glDeleteBuffers(1, &m_CurveBuffer);
        glDeleteBuffers(1, &m_BandBuffer);
        glDeleteTextures(1, &m_CurveTexture);
        glDeleteTextures(1, &m_BandTexture);
        glDeleteVertexArrays(1, &m_Vao);
    }

    void Shape(Rect bounds, Color color, Color border, Float4 params, Rect clip, int kind = 0, int gradient = 0,
               int shadow = 0)
    {
        if (bounds.m_Width <= 0 || bounds.m_Height <= 0)
            return;
        auto [scissor, effects] = DrawingState(clip);
        if (effects[1] < 0)
            return;
        const float padding = params[3];
        if (!Visible({bounds.m_X - padding, bounds.m_Y - padding, bounds.m_Width + 2 * padding,
                      bounds.m_Height + 2 * padding},
                     scissor))
            return;
        effects[2] = gradient;
        effects[3] = shadow;
        ++m_ShapeCount;
        Record({Values(bounds), params, color, border, Values(scissor), {}, {0, 0, 0, kind}, effects});
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

Renderer::Renderer(FontFace& font)
{
    font.VerifyAccess();
    m_Impl = std::make_unique<Impl>(font);
    static_assert(sizeof(Impl::Instance) == 128 && sizeof(int) == 4,
                  "The GL vertex attribute stream requires a packed 128-byte instance and 32-bit integers.");
    if (!GLAD_GL_VERSION_3_3)
        throw std::runtime_error("OpenGL 3.3 core is required.");
    m_Impl->m_Program = Program(UnifiedVertex, UnifiedFragment);
    m_Impl->m_ViewportLocation = glGetUniformLocation(m_Impl->m_Program, "uViewport");
    glUseProgram(m_Impl->m_Program);
    glUniform1i(glGetUniformLocation(m_Impl->m_Program, "uCurves"), 0);
    glUniform1i(glGetUniformLocation(m_Impl->m_Program, "uBands"), 1);
    glUniform1i(glGetUniformLocation(m_Impl->m_Program, "uEffects"), 3);
    glUniform1i(glGetUniformLocation(m_Impl->m_Program, "uImage"), 2);
    glGenBuffers(1, &m_Impl->m_EffectBuffer);
    glGenTextures(1, &m_Impl->m_EffectTexture);
    glGenTextures(1, &m_Impl->m_RetainedTexture);
    glGenFramebuffers(1, &m_Impl->m_RetainedFramebuffer);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_Impl->m_MaxTextureSize);
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &m_Impl->m_MaxBufferSize);
    glGenVertexArrays(1, &m_Impl->m_Vao);
    glGenBuffers(1, &m_Impl->m_InstanceBuffer);
    glGenBuffers(1, &m_Impl->m_CurveBuffer);
    glGenBuffers(1, &m_Impl->m_BandBuffer);
    glGenTextures(1, &m_Impl->m_CurveTexture);
    glGenTextures(1, &m_Impl->m_BandTexture);
}

Renderer::~Renderer()
{
    if (!m_Affinity.IsOwnerThread())
        std::terminate();
}

void Renderer::BeginFrame(float width, float height, int framebufferWidth, int framebufferHeight, Color clear)
{
    m_Affinity.VerifyAccess("Renderer::BeginFrame");
    auto& self = *m_Impl;
    self.PollTimers();
    self.m_Clear = clear;
    self.m_Width = std::max(1.0f, width);
    self.m_Height = std::max(1.0f, height);
    self.m_FramebufferWidth = std::max(1, framebufferWidth);
    self.m_FramebufferHeight = std::max(1, framebufferHeight);
    self.m_Instances.clear();
    self.m_PaintKeys.clear();
    self.m_OwnerCounters.clear();
    self.m_Images.clear();
    self.m_Effects = {{1, 0, 0, 0}, {0, 1, 0, 0}};
    self.m_States.clear();
    Impl::State state;
    state.m_Scissor = {0, 0, self.m_Width, self.m_Height};
    self.m_States.push_back(state);
    self.m_GradientCount = self.m_ShadowCount = 0;
    self.m_ShapeCount = 0;
    self.m_GlyphCount = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, self.m_FramebufferWidth, self.m_FramebufferHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::PushState(Affine local)
{
    m_Affinity.VerifyAccess("Renderer::PushState");
    auto& self = *m_Impl;
    auto state = self.m_States.back();
    state.m_Before = nullptr;
    state.m_Rounded = false;
    state.m_IgnoredClip = {};
    if (local != Affine{})
    {
        state.m_Transform = state.m_Transform * local;
        state.m_TransformIndex = static_cast<int>(self.m_Effects.size());
        const auto& t = state.m_Transform;
        self.m_Effects.push_back({t.m_A, t.m_C, t.m_X, 0});
        self.m_Effects.push_back({t.m_B, t.m_D, t.m_Y, 0});
    }
    self.m_States.push_back(state);
}

void Renderer::PopState()
{
    m_Affinity.VerifyAccess("Renderer::PopState");
    if (m_Impl->m_States.size() <= 1)
        throw std::logic_error("Unbalanced renderer state stack.");
    m_Impl->m_States.pop_back();
}

void Renderer::PushClip(Rect bounds, float radius)
{
    m_Affinity.VerifyAccess("Renderer::PushClip");
    auto& self = *m_Impl;
    auto& state = self.m_States.back();
    if (radius <= 0 && state.m_Transform.m_B == 0 && state.m_Transform.m_C == 0)
        state.m_Scissor = state.m_Scissor.Intersect(state.m_Transform.MapBounds(bounds));
    else
    {
        state.m_ClipIndex = self.AppendClip(bounds, radius, state);
        ++state.m_ClipDepth;
    }
}

void Renderer::SetWidgetPaint(Rect bounds, Rect inheritedClip, const Style* before, bool rounded, float radius,
                              std::uintptr_t ownerIdentity)
{
    m_Affinity.VerifyAccess("Renderer::SetWidgetPaint");
    auto& state = m_Impl->m_States.back();
    state.m_Owner = bounds;
    state.m_OwnerIdentity = ownerIdentity;
    state.m_SubIdentity = 0;
    state.m_CustomIdentity = false;
    state.m_Layer = 0;
    state.m_IgnoredClip = inheritedClip;
    state.m_Before = before;
    state.m_BeforeDrawn = false;
    state.m_Rounded = rounded;
    state.m_Radius = radius;
}

void Renderer::SetPaintIdentity(std::uintptr_t ownerIdentity, std::uint64_t subIdentity)
{
    m_Affinity.VerifyAccess("Renderer::SetPaintIdentity");
    auto& state = m_Impl->m_States.back();
    state.m_OwnerIdentity = ownerIdentity;
    state.m_SubIdentity = subIdentity;
    state.m_CustomIdentity = true;
    state.m_Layer = 0;
}

void Renderer::DrawDecoration(Rect owner, const Style& style, Rect clip, float opacity, unsigned layer)
{
    m_Affinity.VerifyAccess("Renderer::DrawDecoration");
    if (layer < 1 || layer > 2)
        throw std::invalid_argument("Decoration layer must be 1 (before) or 2 (after).");
    const auto& edges = style.m_Anchors;
    const float left = edges.m_Left.Resolve(owner.m_Width, 0);
    const float right = edges.m_Right.Resolve(owner.m_Width, 0);
    const float top = edges.m_Top.Resolve(owner.m_Height, 0);
    const float bottom = edges.m_Bottom.Resolve(owner.m_Height, 0);
    const float width = std::clamp(style.m_Width.Resolve(owner.m_Width, std::max(0.0f, owner.m_Width - left - right)),
                                   style.m_MinWidth, style.m_MaxWidth);
    const float height =
        std::clamp(style.m_Height.Resolve(owner.m_Height, std::max(0.0f, owner.m_Height - top - bottom)),
                   style.m_MinHeight, style.m_MaxHeight);
    Rect bounds{owner.m_X + (edges.m_Left.m_Unit == Length::Unit::Auto && edges.m_Right.m_Unit != Length::Unit::Auto
                                 ? owner.m_Width - right - width
                                 : left),
                owner.m_Y + (edges.m_Top.m_Unit == Length::Unit::Auto && edges.m_Bottom.m_Unit != Length::Unit::Auto
                                 ? owner.m_Height - bottom - height
                                 : top),
                width, height};
    bounds.m_X += style.m_TranslateX;
    bounds.m_Y += style.m_TranslateY;
    PushState(style.m_Transform.Resolve(bounds));
    // The owner clip was installed before entering the decorative transform.
    m_Impl->m_States.back().m_IgnoredClip = clip;
    m_Impl->m_States.back().m_Layer = layer;
    DrawBox(bounds, style, clip, opacity * style.m_Opacity);
    PopState();
}

void Renderer::DrawBox(Rect bounds, const Style& style, Rect clip, float opacity)
{
    m_Affinity.VerifyAccess("Renderer::DrawBox");
    auto& self = *m_Impl;
    auto fill = style.m_Background;
    auto border = style.m_BorderColor;
    fill.m_A *= opacity;
    border.m_A *= opacity;
    // Outer shadows must remain outside the owner's optional rounded content clip.
    const bool rounded = self.m_States.back().m_Rounded;
    self.m_States.back().m_Rounded = false;
    const auto shadowShape = [&](const Shadow& shadow)
    {
        if (shadow.m_Color.m_A <= 0)
            return;
        auto color = shadow.m_Color;
        color.m_A *= opacity;
        const int index = static_cast<int>(self.m_Effects.size());
        if (!shadow.m_Inset)
        {
            self.m_Effects.push_back(Values(bounds));
            self.m_Effects.push_back({style.m_Radius, 0, 0, 0});
            const Rect cast{bounds.m_X + shadow.m_X - shadow.m_Spread, bounds.m_Y + shadow.m_Y - shadow.m_Spread,
                            std::max(0.0f, bounds.m_Width + 2 * shadow.m_Spread),
                            std::max(0.0f, bounds.m_Height + 2 * shadow.m_Spread)};
            self.Shape(cast, color, {},
                       {std::max(0.0f, style.m_Radius + shadow.m_Spread), 0, shadow.m_Blur, shadow.m_Blur + 1}, clip, 4,
                       0, index);
        }
        else
        {
            const float inset = std::min(style.m_BorderWidth, std::min(bounds.m_Width, bounds.m_Height) * 0.5f);
            const Rect inner{bounds.m_X + inset, bounds.m_Y + inset, bounds.m_Width - 2 * inset,
                             bounds.m_Height - 2 * inset};
            const Rect hole{inner.m_X + shadow.m_X + shadow.m_Spread, inner.m_Y + shadow.m_Y + shadow.m_Spread,
                            std::max(0.0f, inner.m_Width - 2 * shadow.m_Spread),
                            std::max(0.0f, inner.m_Height - 2 * shadow.m_Spread)};
            const float radius = std::max(0.0f, style.m_Radius - inset);
            self.m_Effects.push_back(Values(hole));
            self.m_Effects.push_back({std::max(0.0f, radius - shadow.m_Spread), shadow.m_Blur, 0, 0});
            self.Shape(inner, color, {}, {radius, 0, 0, 0}, clip, 3, 0, index);
        }
        ++self.m_ShadowCount;
    };
    for (auto i = style.m_ShadowLayers.m_Layers.rbegin(); i != style.m_ShadowLayers.m_Layers.rend(); ++i)
        if (!i->m_Inset)
            shadowShape(*i);
    if (!style.m_Shadow.m_Inset)
        shadowShape(style.m_Shadow);
    if (fill.m_A > 0 || style.m_BackgroundImage.m_Kind != Gradient::Kind::None ||
        (style.m_BorderWidth > 0 && border.m_A > 0))
        self.Shape(bounds, fill, border, {style.m_Radius, style.m_BorderWidth, 0, 1}, clip, 0,
                   self.AddGradient(style.m_BackgroundImage, opacity, bounds));
    for (auto i = style.m_ShadowLayers.m_Layers.rbegin(); i != style.m_ShadowLayers.m_Layers.rend(); ++i)
        if (i->m_Inset)
            shadowShape(*i);
    if (style.m_Shadow.m_Inset)
        shadowShape(style.m_Shadow);
    self.m_States.back().m_Rounded = rounded;
    const auto state = self.m_States.back();
    if (state.m_Before && !state.m_BeforeDrawn && state.m_Owner == bounds)
    {
        self.m_States.back().m_BeforeDrawn = true;
        PushState();
        if (rounded)
            PushClip(bounds, style.m_Radius);
        DrawDecoration(bounds, *state.m_Before, clip, opacity);
        PopState();
    }
}

void Renderer::DrawSolid(Rect bounds, Color color, Rect clip)
{
    m_Affinity.VerifyAccess("Renderer::DrawSolid");
    if (color.m_A > 0)
        m_Impl->Shape(bounds, color, {}, {0, 0, 0, 1}, clip);
    else
        // Preserve logical slots for transparent row/cell backgrounds so a
        // selection change does not invalidate surrounding grid primitives.
        m_Impl->NextPaintKey(0);
}

void Renderer::DrawLine(Point from, Point to, Color color, float width, Rect clip)
{
    m_Affinity.VerifyAccess("Renderer::DrawLine");
    if (!std::isfinite(from.m_X) || !std::isfinite(from.m_Y) || !std::isfinite(to.m_X) || !std::isfinite(to.m_Y) ||
        !std::isfinite(width))
        throw std::invalid_argument("Line coordinates and width must be finite.");
    auto& self = *m_Impl;
    if (width <= 0 || color.m_A <= 0)
    {
        self.NextPaintKey(5);
        return;
    }
    const float radius = width * 0.5f;
    const float padding = std::max(1.0f, std::max(self.m_Width / static_cast<float>(self.m_FramebufferWidth),
                                                  self.m_Height / static_cast<float>(self.m_FramebufferHeight)));
    const float margin = radius + padding;
    // The rasterized quad follows the segment. Its conservative axis-aligned
    // bounds are used only for damage and coarse clipping, never as a filled quad.
    const Rect bounds{std::min(from.m_X, to.m_X) - margin, std::min(from.m_Y, to.m_Y) - margin,
                      std::abs(to.m_X - from.m_X) + 2 * margin, std::abs(to.m_Y - from.m_Y) + 2 * margin};
    const auto [scissor, effects] = self.DrawingState(clip);
    if (effects[1] < 0 || !self.Visible(bounds, scissor))
        return;
    ++self.m_ShapeCount;
    self.Record({Values(bounds),
                 {from.m_X, from.m_Y, to.m_X, to.m_Y},
                 color,
                 {},
                 Values(scissor),
                 {radius, padding, 0, 0},
                 {0, 0, 0, 5},
                 effects});
}

void Renderer::DrawCircle(Point center, float radius, const Style& style, Rect clip, float opacity)
{
    m_Affinity.VerifyAccess("Renderer::DrawCircle");
    if (!std::isfinite(center.m_X) || !std::isfinite(center.m_Y) || !std::isfinite(radius))
        throw std::invalid_argument("Circle coordinates and radius must be finite.");
    if (radius <= 0)
        return;
    auto circleStyle = style;
    circleStyle.m_Radius = radius;
    DrawBox({center.m_X - radius, center.m_Y - radius, radius * 2, radius * 2}, circleStyle, clip, opacity);
}

void Renderer::DrawText(const TextLayout& layout, Point baseline, Color color, Rect clip)
{
    m_Affinity.VerifyAccess("Renderer::DrawText");
    if (clip.m_Width <= 0 || clip.m_Height <= 0 || color.m_A <= 0 || layout.m_FontSize <= 0)
        return;
    auto& self = *m_Impl;
    const auto [scissor, effects] = self.DrawingState(clip);
    if (effects[1] < 0)
        return;
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
        if (!self.Visible(rect, scissor))
            continue;
        ++self.m_GlyphCount;
        self.Record({Values(rect),
                     {bounds.m_MinX - paddingX / size, bounds.m_MaxY + paddingY / size, rect.m_Width / size,
                      -rect.m_Height / size},
                     color,
                     {},
                     Values(scissor),
                     glyph.m_Transform,
                     {glyph.m_Start, glyph.m_Count, glyph.m_Count, 1},
                     effects});
    }
}

void Renderer::DrawTexture(Rect bounds, std::uint32_t texture, Rect clip, float opacity, std::uint64_t revision)
{
    m_Affinity.VerifyAccess("Renderer::DrawTexture");
    if (!texture || bounds.m_Width <= 0 || bounds.m_Height <= 0)
        return;
    auto& self = *m_Impl;
    const auto [scissor, effects] = self.DrawingState(clip);
    if (effects[1] < 0 || !self.Visible(bounds, scissor))
        return;
    self.m_Images.push_back({self.m_Instances.size(), texture, revision});
    self.Record({Values(bounds), {0, 1, 1, -1}, {1, 1, 1, opacity}, {}, Values(scissor), {}, {0, 0, 0, 2}, effects});
}

void Renderer::SetPartialRedrawEnabled(bool enabled)
{
    m_Affinity.VerifyAccess("Renderer::SetPartialRedrawEnabled");
    if (m_Impl->m_PartialRedraw != enabled)
    {
        m_Impl->m_PartialRedraw = enabled;
        m_Impl->m_HistoryValid = false;
    }
}

bool Renderer::IsPartialRedrawEnabled() const
{
    m_Affinity.VerifyAccess("Renderer::IsPartialRedrawEnabled");
    return m_Impl->m_PartialRedraw;
}

void Renderer::SetGpuTimingEnabled(bool enabled)
{
    m_Affinity.VerifyAccess("Renderer::SetGpuTimingEnabled");
    auto& self = *m_Impl;
    if (enabled && !self.m_Queries[0])
        glGenQueries(static_cast<GLsizei>(self.m_Queries.size()), self.m_Queries.data());
    self.m_TimingEnabled = enabled;
}

void Renderer::EndFrame()
{
    m_Affinity.VerifyAccess("Renderer::EndFrame");
    auto& self = *m_Impl;
    self.m_FrameReady = false;
    self.EnsureRetainedTarget();
    self.BuildDamage();
    self.BuildBatches();
    const bool submitted = !self.m_Submitted.empty();
    if (submitted && self.m_FontDirty &&
        (self.m_Curves.size() > static_cast<std::size_t>(self.m_MaxBufferSize) ||
         self.m_Bands.size() > static_cast<std::size_t>(self.m_MaxBufferSize)))
        throw std::runtime_error("Glyph cache exceeds the texture buffer limit.");
    if (submitted && self.m_Effects.size() > static_cast<std::size_t>(self.m_MaxBufferSize))
        throw std::runtime_error("Frame effects exceed the texture buffer limit.");
    if (self.m_Submitted.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
        throw std::runtime_error("Frame exceeds the GL instance limit.");
    self.StartTimer();
    glBindVertexArray(self.m_Vao);
    if (submitted && self.m_FontDirty)
    {
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
    if (submitted)
    {
        glBindBuffer(GL_TEXTURE_BUFFER, self.m_EffectBuffer);
        glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(self.m_Effects.size() * sizeof(Float4)),
                     self.m_Effects.data(), GL_STREAM_DRAW);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_BUFFER, self.m_EffectTexture);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, self.m_EffectBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, self.m_InstanceBuffer);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(self.m_Submitted.size() * sizeof(Impl::Instance)),
                     self.m_Submitted.data(), GL_STREAM_DRAW);
        glUseProgram(self.m_Program);
        glUniform2f(self.m_ViewportLocation, self.m_Width, self.m_Height);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, self.m_CurveTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, self.m_BandTexture);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, self.m_RetainedFramebuffer);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(self.m_Clear.m_R, self.m_Clear.m_G, self.m_Clear.m_B, self.m_Clear.m_A);
    std::size_t batchIndex = 0, calls = 0;
    for (const auto& damage : self.m_Damage)
    {
        glScissor(damage.m_X, self.m_FramebufferHeight - damage.m_Y - damage.m_Height, damage.m_Width, damage.m_Height);
        glClear(GL_COLOR_BUFFER_BIT);
        while (batchIndex < self.m_Batches.size() && self.m_Batches[batchIndex].m_Damage == damage)
        {
            const auto& batch = self.m_Batches[batchIndex++];
            const auto offset = batch.m_Begin * sizeof(Impl::Instance);
            for (unsigned i = 0; i < 6; ++i)
                Impl::FloatAttribute(i, sizeof(Impl::Instance), offset + i * sizeof(Float4));
            for (unsigned i = 6; i < 8; ++i)
            {
                glEnableVertexAttribArray(i);
                const auto member = i == 6 ? offsetof(Impl::Instance, m_Data) : offsetof(Impl::Instance, m_Effects);
                glVertexAttribIPointer(i, 4, GL_INT, sizeof(Impl::Instance),
                                       reinterpret_cast<const void*>(offset + member));
                glVertexAttribDivisor(i, 1);
            }
            if (batch.m_Texture)
            {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, batch.m_Texture);
            }
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(batch.m_End - batch.m_Begin));
            ++calls;
        }
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, self.m_RetainedFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    // The native back buffer is not guaranteed to survive buffer swaps. Present
    // the retained result with a cheap full copy, never relying on swap preservation.
    glBlitFramebuffer(0, 0, self.m_FramebufferWidth, self.m_FramebufferHeight, 0, 0, self.m_FramebufferWidth,
                      self.m_FramebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (self.m_ActiveQuery >= 0)
    {
        glEndQuery(GL_TIME_ELAPSED);
        self.m_QueryPending[static_cast<std::size_t>(self.m_ActiveQuery)] = true;
        self.m_ActiveQuery = -1;
    }
    self.m_Stats = {};
    auto& stats = self.m_Stats;
    stats.m_DrawCalls = calls;
    stats.m_Shapes = self.m_ShapeCount;
    stats.m_Glyphs = self.m_GlyphCount;
    stats.m_CachedGlyphs = self.m_GlyphCache.size();
    stats.m_FontBytes = self.m_Curves.size() * sizeof(Float4) + self.m_Bands.size() * sizeof(UInt2);
    stats.m_InstanceBytes = self.m_Submitted.size() * sizeof(Impl::Instance);
    stats.m_EffectBytes = submitted ? self.m_Effects.size() * sizeof(Float4) : 0;
    stats.m_Gradients = self.m_GradientCount;
    stats.m_Shadows = self.m_ShadowCount;
    stats.m_RepaintPixels = self.m_RepaintPixels;
    stats.m_FramebufferPixels =
        static_cast<std::size_t>(self.m_FramebufferWidth) * static_cast<std::size_t>(self.m_FramebufferHeight);
    stats.m_SubmittedInstances = self.m_Submitted.size();
    stats.m_ReplayedGlyphs = self.m_ReplayedGlyphs;
    stats.m_DamageRectangles = self.m_Damage.size();
    stats.m_GpuMilliseconds = self.m_GpuMilliseconds;
    stats.m_TimingSamples = self.m_TimingSamples;
    self.m_PreviousKeys.swap(self.m_PaintKeys);
    self.m_PreviousInstances.swap(self.m_Instances);
    self.m_PreviousEffects.swap(self.m_Effects);
    self.m_PreviousBounds.swap(self.m_Bounds);
    self.m_PreviousTextures.swap(self.m_TextureStates);
    self.m_PreviousWidth = self.m_Width;
    self.m_PreviousHeight = self.m_Height;
    self.m_PreviousClear = self.m_Clear;
    self.m_HistoryValid = true;
    const auto error = glGetError();
    if (error != GL_NO_ERROR)
    {
        self.m_HistoryValid = false;
        throw std::runtime_error("OpenGL error after rendering: " + std::to_string(error));
    }
    self.m_FrameReady = true;
}

Color Renderer::ReadPixel(Point point) const
{
    m_Affinity.VerifyAccess("Renderer::ReadPixel");
    const auto& self = *m_Impl;
    if (!self.m_FrameReady)
        throw std::logic_error("Renderer capture requires a successfully completed frame.");
    if (!std::isfinite(point.m_X) || !std::isfinite(point.m_Y))
        throw std::invalid_argument("Renderer capture coordinates must be finite.");
    const auto coordinate = [](float value, float extent, int pixels)
    {
        const double scaled = static_cast<double>(value) * static_cast<double>(pixels) / extent;
        return static_cast<int>(std::clamp(scaled, 0.0, static_cast<double>(pixels - 1)));
    };
    const int x = coordinate(point.m_X, self.m_PreviousWidth, self.m_RetainedWidth);
    const int y = self.m_RetainedHeight - 1 - coordinate(point.m_Y, self.m_PreviousHeight, self.m_RetainedHeight);
    unsigned char pixel[4]{};
    const FrameReadScope reading(self.m_RetainedFramebuffer);
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return {pixel[0] / 255.0f, pixel[1] / 255.0f, pixel[2] / 255.0f, pixel[3] / 255.0f};
}

void Renderer::SavePpm(const std::filesystem::path& path) const
{
    m_Affinity.VerifyAccess("Renderer::SavePpm");
    const auto& self = *m_Impl;
    if (!self.m_FrameReady)
        throw std::logic_error("Renderer capture requires a successfully completed frame.");
    std::vector<unsigned char> pixels(static_cast<std::size_t>(self.m_RetainedWidth) *
                                      static_cast<std::size_t>(self.m_RetainedHeight) * 3);
    {
        const FrameReadScope reading(self.m_RetainedFramebuffer);
        glReadPixels(0, 0, self.m_RetainedWidth, self.m_RetainedHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    }
    std::ofstream output(path, std::ios::binary);
    if (!output)
        throw std::runtime_error("Cannot write screenshot: " + path.string());
    output << "P6\n" << self.m_RetainedWidth << ' ' << self.m_RetainedHeight << "\n255\n";
    const auto stride = static_cast<std::size_t>(self.m_RetainedWidth) * 3;
    for (int y = self.m_RetainedHeight - 1; y >= 0; --y)
        output.write(reinterpret_cast<const char*>(pixels.data() + static_cast<std::size_t>(y) * stride),
                     static_cast<std::streamsize>(stride));
    if (!output)
        throw std::runtime_error("Screenshot write failed.");
}

const RenderStats& Renderer::GetStats() const
{
    m_Affinity.VerifyAccess("Renderer::GetStats");
    return m_Impl->m_Stats;
}

std::string Renderer::GetDriverDescription() const
{
    m_Affinity.VerifyAccess("Renderer::GetDriverDescription");
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    return std::string(renderer ? renderer : "unknown") + " | " + (version ? version : "unknown");
}
} // namespace EStudioUI
