#include "model_import.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace previewmodel
{
namespace
{
struct V3 { float x=0,y=0,z=0; };
struct V2 { float x=0,y=0; };

V3 add(V3 a,V3 b){ return {a.x+b.x,a.y+b.y,a.z+b.z}; }
V3 sub(V3 a,V3 b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }
V3 mul(V3 a,float s){ return {a.x*s,a.y*s,a.z*s}; }
float dot(V3 a,V3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
V3 cross(V3 a,V3 b){ return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
V3 norm(V3 a){ const float l=std::sqrt(std::max(0.0f,dot(a,a))); return l>1e-8f?mul(a,1.0f/l):V3{0,1,0}; }

std::string lower(std::string s){ for(char& c:s)c=(char)std::tolower((unsigned char)c); return s; }
std::string trim(std::string s)
{
    auto notws=[](unsigned char c){return !std::isspace(c);};
    s.erase(s.begin(),std::find_if(s.begin(),s.end(),notws));
    s.erase(std::find_if(s.rbegin(),s.rend(),notws).base(),s.end());
    return s;
}

void finalize(Mesh& m)
{
    if(m.vertices.empty()) return;
    V3 mn{m.vertices[0].position[0],m.vertices[0].position[1],m.vertices[0].position[2]};
    V3 mx=mn;
    for(const auto& v:m.vertices)
    {
        V3 p{v.position[0],v.position[1],v.position[2]};
        mn.x=std::min(mn.x,p.x); mn.y=std::min(mn.y,p.y); mn.z=std::min(mn.z,p.z);
        mx.x=std::max(mx.x,p.x); mx.y=std::max(mx.y,p.y); mx.z=std::max(mx.z,p.z);
    }
    const V3 c=mul(add(mn,mx),0.5f);
    const V3 ext=sub(mx,mn);
    const float largest=std::max({ext.x,ext.y,ext.z,1e-5f});
    const float scale=2.0f/largest;
    for(auto& v:m.vertices)
    {
        V3 p{v.position[0],v.position[1],v.position[2]};
        p=mul(sub(p,c),scale);
        v.position={p.x,p.y,p.z};
    }

    std::vector<V3> tan(m.vertices.size(), V3{});
    for(size_t i=0;i+2<m.indices.size();i+=3)
    {
        const auto ia=m.indices[i], ib=m.indices[i+1], ic=m.indices[i+2];
        if(ia>=m.vertices.size()||ib>=m.vertices.size()||ic>=m.vertices.size()) continue;
        auto& a=m.vertices[ia]; auto& b=m.vertices[ib]; auto& c0=m.vertices[ic];
        V3 p0{a.position[0],a.position[1],a.position[2]}, p1{b.position[0],b.position[1],b.position[2]}, p2{c0.position[0],c0.position[1],c0.position[2]};
        V2 w0{a.uv[0],a.uv[1]}, w1{b.uv[0],b.uv[1]}, w2{c0.uv[0],c0.uv[1]};
        const V3 e1=sub(p1,p0), e2=sub(p2,p0);
        const float du1=w1.x-w0.x,dv1=w1.y-w0.y,du2=w2.x-w0.x,dv2=w2.y-w0.y;
        const float det=du1*dv2-du2*dv1;
        V3 t = std::abs(det)>1e-8f ? mul(sub(mul(e1,dv2),mul(e2,dv1)),1.0f/det) : V3{1,0,0};
        tan[ia]=add(tan[ia],t); tan[ib]=add(tan[ib],t); tan[ic]=add(tan[ic],t);
    }
    for(size_t i=0;i<m.vertices.size();++i)
    {
        V3 n{m.vertices[i].normal[0],m.vertices[i].normal[1],m.vertices[i].normal[2]}; n=norm(n);
        V3 t=tan[i]; t=sub(t,mul(n,dot(n,t))); t=norm(t);
        if(dot(t,t)<0.1f) t={1,0,0};
        m.vertices[i].normal={n.x,n.y,n.z};
        m.vertices[i].tangent={t.x,t.y,t.z,1.0f};
    }
}

bool loadObj(const std::string& path,Mesh& out,std::string& error)
{
    std::ifstream f(path); if(!f){error="Could not open OBJ file.";return false;}
    std::vector<V3> pos,nrm; std::vector<V2> uv;
    struct Key{int p=-1,t=-1,n=-1; bool operator==(const Key&o)const{return p==o.p&&t==o.t&&n==o.n;}};
    struct Hash{size_t operator()(const Key&k)const{return (size_t)(k.p+1)*73856093u^(size_t)(k.t+2)*19349663u^(size_t)(k.n+3)*83492791u;}};
    std::unordered_map<Key,std::uint32_t,Hash> map;
    auto fixIndex=[](int i,int n){return i>0?i-1:(i<0?n+i:-1);};
    std::string line;
    while(std::getline(f,line))
    {
        line=trim(line); if(line.empty()||line[0]=='#')continue;
        std::istringstream ss(line); std::string op; ss>>op;
        if(op=="v"){V3 v;ss>>v.x>>v.y>>v.z;pos.push_back(v);}
        else if(op=="vn"){V3 v;ss>>v.x>>v.y>>v.z;nrm.push_back(norm(v));}
        else if(op=="vt"){V2 v;ss>>v.x>>v.y;uv.push_back(v);}
        else if(op=="f")
        {
            std::vector<std::uint32_t> face; std::string tok;
            while(ss>>tok)
            {
                int pi=0,ti=0,ni=0; char slash=0;
                std::istringstream ts(tok); ts>>pi;
                if(ts.peek()=='/'){ts>>slash;if(ts.peek()!='/')ts>>ti;if(ts.peek()=='/'){ts>>slash;ts>>ni;}}
                Key k{fixIndex(pi,(int)pos.size()),fixIndex(ti,(int)uv.size()),fixIndex(ni,(int)nrm.size())};
                if(k.p<0||k.p>=(int)pos.size()) continue;
                auto it=map.find(k);
                if(it==map.end())
                {
                    Vertex v; const auto p=pos[k.p];v.position={p.x,p.y,p.z};
                    if(k.t>=0&&k.t<(int)uv.size())v.uv={uv[k.t].x,1.0f-uv[k.t].y};
                    if(k.n>=0&&k.n<(int)nrm.size())v.normal={nrm[k.n].x,nrm[k.n].y,nrm[k.n].z};
                    const auto idx=(std::uint32_t)out.vertices.size(); out.vertices.push_back(v); map[k]=idx; face.push_back(idx);
                } else face.push_back(it->second);
            }
            for(size_t i=1;i+1<face.size();++i) out.indices.insert(out.indices.end(),{face[0],face[i],face[i+1]});
        }
    }
    if(out.indices.empty()){error="OBJ contains no polygon faces.";return false;}
    if(nrm.empty())
    {
        std::vector<V3> accum(out.vertices.size(), V3{});
        for(size_t i=0;i+2<out.indices.size();i+=3){auto a=out.indices[i],b=out.indices[i+1],c=out.indices[i+2];V3 p0{out.vertices[a].position[0],out.vertices[a].position[1],out.vertices[a].position[2]},p1{out.vertices[b].position[0],out.vertices[b].position[1],out.vertices[b].position[2]},p2{out.vertices[c].position[0],out.vertices[c].position[1],out.vertices[c].position[2]};V3 n=cross(sub(p1,p0),sub(p2,p0));accum[a]=add(accum[a],n);accum[b]=add(accum[b],n);accum[c]=add(accum[c],n);}
        for(size_t i=0;i<out.vertices.size();++i){auto n=norm(accum[i]);out.vertices[i].normal={n.x,n.y,n.z};}
    }
    out.sourceFormat="OBJ"; finalize(out); return true;
}

std::vector<double> fbxArray(const std::string& text,const std::string& name)
{
    std::vector<double> r; const auto p=text.find(name+":"); if(p==std::string::npos)return r;
    auto a=text.find("a:",p); if(a==std::string::npos)return r; a+=2;
    auto e=text.find('}',a); if(e==std::string::npos)e=text.size();
    std::string s=text.substr(a,e-a); for(char&c:s)if(c==',')c=' ';
    std::istringstream ss(s); double v; while(ss>>v)r.push_back(v); return r;
}

bool loadAsciiFbx(const std::string& path,Mesh& out,std::string& error)
{
    std::ifstream f(path,std::ios::binary); if(!f){error="Could not open FBX file.";return false;}
    std::string text((std::istreambuf_iterator<char>(f)),{});
    if(text.rfind("Kaydara FBX Binary",0)==0){error="Binary FBX is not handled by the built-in parser yet. Export ASCII FBX (2013-2020) or OBJ.";return false;}
    const auto verts=fbxArray(text,"Vertices"), poly=fbxArray(text,"PolygonVertexIndex"), uvs=fbxArray(text,"UV"), uvIdx=fbxArray(text,"UVIndex"), normals=fbxArray(text,"Normals");
    if(verts.size()<9||poly.empty()){error="FBX does not contain readable Mesh Vertices/PolygonVertexIndex arrays.";return false;}
    std::vector<V3> p;for(size_t i=0;i+2<verts.size();i+=3)p.push_back({(float)verts[i],(float)verts[i+1],(float)verts[i+2]});
    std::vector<std::uint32_t> face; size_t corner=0;
    auto emitCorner=[&](int pi,size_t c)->std::uint32_t
    {
        Vertex v; auto q=p[(size_t)std::clamp(pi,0,(int)p.size()-1)]; v.position={q.x,q.y,q.z};
        if(!uvs.empty())
        {
            int ui=(int)c; if(c<uvIdx.size())ui=(int)uvIdx[c];
            const size_t u=(size_t)std::max(0,ui)*2; if(u+1<uvs.size())v.uv={(float)uvs[u],1.0f-(float)uvs[u+1]};
        }
        if(normals.size()>=(c+1)*3){v.normal={(float)normals[c*3],(float)normals[c*3+1],(float)normals[c*3+2]};}
        out.vertices.push_back(v); return (std::uint32_t)out.vertices.size()-1;
    };
    for(double dv:poly)
    {
        int raw=(int)dv; bool end=raw<0; int pi=end?(-raw-1):raw; if(pi>=0&&pi<(int)p.size())face.push_back(emitCorner(pi,corner)); ++corner;
        if(end){for(size_t i=1;i+1<face.size();++i)out.indices.insert(out.indices.end(),{face[0],face[i],face[i+1]});face.clear();}
    }
    if(out.indices.empty()){error="FBX mesh contains no triangulatable polygons.";return false;}
    if(normals.empty())
    {
        for(size_t i=0;i+2<out.indices.size();i+=3){auto a=out.indices[i],b=out.indices[i+1],c=out.indices[i+2];V3 p0{out.vertices[a].position[0],out.vertices[a].position[1],out.vertices[a].position[2]},p1{out.vertices[b].position[0],out.vertices[b].position[1],out.vertices[b].position[2]},p2{out.vertices[c].position[0],out.vertices[c].position[1],out.vertices[c].position[2]};V3 n=norm(cross(sub(p1,p0),sub(p2,p0)));for(auto idx:{a,b,c})out.vertices[idx].normal={n.x,n.y,n.z};}
    }
    out.sourceFormat="FBX (ASCII)"; finalize(out); return true;
}

bool parseTriple(const std::string& line,V3& v)
{
    auto p=line.find(' '); if(p==std::string::npos)return false; std::string s=line.substr(p+1); for(char&c:s)if(c==',')c=' '; std::istringstream ss(s); return bool(ss>>v.x>>v.y>>v.z);
}
bool parseUv(const std::string& line,V2& v)
{
    auto p=line.find(' '); if(p==std::string::npos)return false; std::string s=line.substr(p+1); for(char&c:s)if(c==',')c=' '; std::istringstream ss(s); int set=0; if(!(ss>>set))return false; return bool(ss>>v.x>>v.y);
}


// BO3 XMODEL_BIN files are an LZ4-compressed token stream. The token layout is
// the binary counterpart of XMODEL_EXPORT, so we can recover the exact authored
// per-corner UVs/normals instead of approximating APE's preview meshes.
//
// This reader intentionally implements only the stable token vocabulary used by
// BO3 model exports. It does not depend on an external LZ4 DLL, which keeps the
// portable Studio build self-contained while still reading the user's local Mod
// Tools assets at runtime.
bool decompressLz4Block(const std::vector<std::uint8_t>& src, size_t expectedSize,
                        std::vector<std::uint8_t>& dst, std::string& error)
{
    dst.clear();
    dst.resize(expectedSize);
    size_t ip=0, op=0;
    auto readLen=[&](size_t base,size_t& len)->bool
    {
        len=base;
        if(base!=15) return true;
        while(ip<src.size())
        {
            const std::uint8_t b=src[ip++];
            len+=b;
            if(b!=255) return true;
        }
        return false;
    };
    while(ip<src.size())
    {
        const std::uint8_t token=src[ip++];
        size_t literalLen=0;
        if(!readLen(token>>4,literalLen)){error="Truncated LZ4 literal length.";return false;}
        if(ip+literalLen>src.size()||op+literalLen>dst.size()){error="Invalid LZ4 literal range.";return false;}
        if(literalLen){std::memcpy(dst.data()+op,src.data()+ip,literalLen);ip+=literalLen;op+=literalLen;}
        if(ip>=src.size()) break;
        if(ip+2>src.size()){error="Truncated LZ4 match offset.";return false;}
        const size_t offset=(size_t)src[ip]|((size_t)src[ip+1]<<8); ip+=2;
        if(offset==0||offset>op){error="Invalid LZ4 match offset.";return false;}
        size_t matchLen=0;
        if(!readLen(token&0x0f,matchLen)){error="Truncated LZ4 match length.";return false;}
        matchLen+=4;
        if(op+matchLen>dst.size()){error="Invalid LZ4 match range.";return false;}
        const size_t matchPos=op-offset;
        for(size_t i=0;i<matchLen;++i) dst[op++]=dst[matchPos+i];
    }
    if(op!=expectedSize)
    {
        error="XMODEL_BIN LZ4 payload expanded to an unexpected size.";
        return false;
    }
    return true;
}

struct XBinCursor
{
    const std::vector<std::uint8_t>& b;
    size_t p=0;
    bool ok=true;
    void align(size_t n){p=(p+n-1)&~(n-1);if(p>b.size())ok=false;}
    template<class T> T read()
    {
        T v{};
        if(!ok||p+sizeof(T)>b.size()){ok=false;return v;}
        std::memcpy(&v,b.data()+p,sizeof(T));p+=sizeof(T);return v;
    }
    std::string stringZ()
    {
        if(!ok||p>=b.size()){ok=false;return {};}
        const size_t begin=p;
        while(p<b.size()&&b[p]!=0)++p;
        if(p>=b.size()){ok=false;return {};}
        std::string s((const char*)b.data()+begin,p-begin);++p;align(4);return s;
    }
};

enum class XBinType { Comment,Section,UShort,UInt,Int,Float,Vec2,Vec3,Vec4,Vec3S16,Vec4U8,BoneWeight,BoneInfo,UShortString,UShortStringX3,Tri,UVSet,Unsupported };
XBinType xbinType(std::uint16_t h)
{
    switch(h)
    {
    case 0x8738: case 0xC355: return XBinType::Comment;
    case 0x7AAC: case 0x46C8: case 0xC7F3: return XBinType::Section;
    case 0xDD9A: case 0xEA46: case 0xBCD4: case 0x92D3: case 0x4643: case 0x76BA:
    case 0x7A6C: case 0xA1B2: case 0x62AF: case 0x9279: case 0x9016: case 0x950D:
    case 0x745A: case 0x24D1: case 0x8F03: return XBinType::UShort;
    case 0xC723: case 0xBE92: case 0xB917: case 0x2AEC: case 0xB097: case 0x1D7D: return XBinType::UInt;
    case 0x1FC2: case 0xB35E: case 0xA65B: case 0x7836: return XBinType::Int;
    case 0x5CD2: return XBinType::Float;
    case 0x83C7: case 0xC835: case 0xFE0C: case 0x7D76: case 0x7E24: return XBinType::Vec2;
    case 0x9383: case 0x1C56: case 0xA58B: return XBinType::Vec3;
    case 0x37FF: case 0x4265: case 0xE593: case 0x317C: case 0x6DAB: case 0xEF69: case 0x6EEE: return XBinType::Vec4;
    case 0x89EC: case 0xDCFD: case 0xCCDC: case 0xFCBF: return XBinType::Vec3S16;
    case 0x6DD8: return XBinType::Vec4U8;
    case 0xF1AB: return XBinType::BoneWeight;
    case 0xF099: return XBinType::BoneInfo;
    case 0x87D4: case 0x360B: return XBinType::UShortString;
    case 0xA700: return XBinType::UShortStringX3;
    case 0x562F: return XBinType::Tri;
    case 0x1AD4: return XBinType::UVSet;
    // TRI16 (0x6711) and FRAME/Unk4 (0x1675) are not emitted by the static
    // APE preview meshes. Stop rather than guessing their payload layout.
    default: return XBinType::Unsupported;
    }
}

bool loadXModelBin(const std::string& path,Mesh& out,std::string& error)
{
    std::ifstream f(path,std::ios::binary);
    if(!f){error="Could not open XMODEL_BIN file.";return false;}
    std::vector<std::uint8_t> packed((std::istreambuf_iterator<char>(f)),{});
    if(packed.size()<10||std::memcmp(packed.data(),"*LZ4*",5)!=0)
    {
        error="This does not look like a BO3 XMODEL_BIN file.";return false;
    }
    std::uint32_t unpackedSize=0;std::memcpy(&unpackedSize,packed.data()+5,4);
    if(unpackedSize==0||unpackedSize>512u*1024u*1024u)
    {
        error="XMODEL_BIN reports an invalid decompressed size.";return false;
    }
    std::vector<std::uint8_t> compressed(packed.begin()+9,packed.end()),data;
    if(!decompressLz4Block(compressed,unpackedSize,data,error))return false;

    XBinCursor r{data};
    std::unordered_map<std::uint32_t,V3> sourcePositions;
    bool inFaces=false,cornerActive=false;
    std::uint32_t pendingSource=0,expectedFaces=0,facesBuilt=0;
    bool pendingSourceValid=false;
    Vertex corner{};std::uint32_t tri[3]{};int triCorner=0;

    auto finalizeCorner=[&]()->bool
    {
        if(!cornerActive)return true;
        if(triCorner>=3){error="XMODEL_BIN face contains more than three vertices.";return false;}
        tri[triCorner++]=(std::uint32_t)out.vertices.size();out.vertices.push_back(corner);cornerActive=false;
        if(triCorner==3)
        {
            out.indices.insert(out.indices.end(),{tri[0],tri[1],tri[2]});
            ++facesBuilt;triCorner=0;
        }
        return true;
    };

    while(r.ok&&r.p<data.size()&&(expectedFaces==0||facesBuilt<expectedFaces))
    {
        r.align(4);if(!r.ok||r.p+2>data.size())break;
        const std::uint16_t h=r.read<std::uint16_t>();
        const XBinType type=xbinType(h);
        if(type==XBinType::Unsupported)
        {
            std::ostringstream ss;ss<<"Unsupported XMODEL_BIN token 0x"<<std::hex<<std::uppercase<<h<<" at byte "<<(r.p-2)<<".";error=ss.str();return false;
        }

        if(h==0xBE92) // NUMFACES
        {
            r.align(4);expectedFaces=r.read<std::uint32_t>();inFaces=true;pendingSourceValid=false;continue;
        }
        if((h==0x8F03||h==0xB097)) // VERT / VERT32
        {
            std::uint32_t id=0;
            if(h==0x8F03){r.align(2);id=r.read<std::uint16_t>();}
            else {r.align(4);id=r.read<std::uint32_t>();}
            if(inFaces)
            {
                if(cornerActive&&!finalizeCorner())return false;
                const auto it=sourcePositions.find(id);
                if(it==sourcePositions.end()){error="XMODEL_BIN face references a vertex with no source position.";return false;}
                corner=Vertex{};corner.position={it->second.x,it->second.y,it->second.z};cornerActive=true;
            }
            else {pendingSource=id;pendingSourceValid=true;}
            continue;
        }
        if(h==0x9383) // OFFSET
        {
            r.align(4);V3 p{r.read<float>(),r.read<float>(),r.read<float>()};
            if(!inFaces&&pendingSourceValid){sourcePositions[pendingSource]=p;pendingSourceValid=false;}
            continue;
        }
        if(h==0x89EC) // NORMAL
        {
            r.align(2);const float k=1.0f/32767.0f;
            V3 n{r.read<std::int16_t>()*k,r.read<std::int16_t>()*k,r.read<std::int16_t>()*k};n=norm(n);
            if(inFaces&&cornerActive)corner.normal={n.x,n.y,n.z};continue;
        }
        if(h==0x1AD4) // UV set
        {
            const std::uint16_t sets=r.read<std::uint16_t>();
            V2 first{};bool have=false;
            for(std::uint16_t i=0;i<sets;++i){V2 uv{r.read<float>(),r.read<float>()};if(!have){first=uv;have=true;}}
            if(inFaces&&cornerActive&&have)
            {
                // Phase 1ac: BO3 XMODEL_BIN is already a compiled/runtime asset.
                // APE's captured material VS forwards its runtime TEXCOORD input
                // directly to the pixel shader (no V inversion), so preserve the
                // packed UV exactly. The old importer treated XMODEL_BIN like a
                // DCC interchange file and flipped V, which mirrored the APE
                // preview sphere checker parity even after the world-frame fix.
                corner.uv={first.x,first.y};
                if(!finalizeCorner())return false;
            }
            continue;
        }
        if(h==0x562F) // TRI: object/material indices; corner tokens follow
        {
            if(cornerActive&&!finalizeCorner())return false;
            if(triCorner!=0){error="Incomplete XMODEL_BIN triangle before TRI token.";return false;}
            r.read<std::uint8_t>();r.read<std::uint8_t>();continue;
        }

        // Consume all remaining stable token payloads so the reader can reach
        // the position/face streams without depending on model-specific layout.
        switch(type)
        {
        case XBinType::Comment: r.align(4);r.stringZ();break;
        case XBinType::Section: break;
        case XBinType::UShort: r.align(2);r.read<std::uint16_t>();break;
        case XBinType::UInt: r.align(4);r.read<std::uint32_t>();break;
        case XBinType::Int: r.align(4);r.read<std::int32_t>();break;
        case XBinType::Float: r.align(4);r.read<float>();break;
        case XBinType::Vec2: r.align(4);r.read<float>();r.read<float>();break;
        case XBinType::Vec3: r.align(4);r.read<float>();r.read<float>();r.read<float>();break;
        case XBinType::Vec4: r.align(4);for(int i=0;i<4;++i)r.read<float>();break;
        case XBinType::Vec3S16: r.align(2);for(int i=0;i<3;++i)r.read<std::int16_t>();break;
        case XBinType::Vec4U8: r.align(4);for(int i=0;i<4;++i)r.read<std::uint8_t>();break;
        case XBinType::BoneWeight: r.align(2);r.read<std::uint16_t>();r.read<float>();break;
        case XBinType::BoneInfo: r.align(4);r.read<std::int32_t>();r.read<std::int32_t>();r.stringZ();break;
        case XBinType::UShortString: r.align(2);r.read<std::uint16_t>();r.stringZ();break;
        case XBinType::UShortStringX3: r.align(2);r.read<std::uint16_t>();r.stringZ();r.stringZ();r.stringZ();break;
        case XBinType::Tri: r.read<std::uint8_t>();r.read<std::uint8_t>();break;
        case XBinType::UVSet:
        {
            const std::uint16_t n=r.read<std::uint16_t>();for(std::uint16_t i=0;i<n;++i){r.read<float>();r.read<float>();}break;
        }
        default: break;
        }
    }
    if(!r.ok){error="Unexpected end of XMODEL_BIN token stream.";return false;}
    if(cornerActive&&!finalizeCorner())return false;
    if(expectedFaces==0||out.indices.empty()){error="XMODEL_BIN contains no readable triangle faces.";return false;}
    if(facesBuilt!=expectedFaces)
    {
        error="XMODEL_BIN face stream ended before all declared faces were read.";return false;
    }
    out.sourceFormat="BO3 XMODEL_BIN";finalize(out);return true;
}

bool loadXModelExport(const std::string& path,Mesh& out,std::string& error)
{
    std::ifstream f(path); if(!f){error="Could not open XMODEL file.";return false;}
    std::vector<std::string> lines;std::string l;while(std::getline(f,l))lines.push_back(trim(l));
    bool sawModel=false;for(const auto&s:lines)if(s=="MODEL"||s.rfind("NUMVERTS",0)==0){sawModel=true;break;}
    if(!sawModel){error="This does not look like a text XMODEL_EXPORT file. Compiled BO3 xmodel assets are not directly readable; export the model to XMODEL_EXPORT/OBJ first.";return false;}

    // First collect the global VERT/OFFSET table used by normal CoD
    // XMODEL_EXPORT files. The same VERT ids are referenced again inside TRI blocks.
    std::unordered_map<int,V3> sourcePositions;
    bool inFaces=false;
    for(size_t i=0;i<lines.size();++i)
    {
        if(lines[i].rfind("NUMFACES",0)==0){inFaces=true;continue;}
        if(inFaces)continue;
        if(lines[i].rfind("VERT ",0)==0)
        {
            int id=-1; std::istringstream vs(lines[i].substr(5)); vs>>id;
            for(size_t j=i+1;j<std::min(lines.size(),i+8);++j)
            {
                if(lines[j].rfind("VERT ",0)==0||lines[j].rfind("NUMFACES",0)==0)break;
                if(lines[j].rfind("OFFSET ",0)==0){V3 p;if(parseTriple(lines[j],p)&&id>=0)sourcePositions[id]=p;break;}
            }
        }
    }

    for(size_t i=0;i<lines.size();++i)
    {
        if(lines[i].rfind("TRI",0)!=0)continue;
        std::uint32_t tri[3]{};
        for(int c=0;c<3;++c)
        {
            while(i+1<lines.size()&&lines[i+1].rfind("VERT ",0)!=0)++i;
            if(i+1>=lines.size()){error="Unexpected end of XMODEL_EXPORT TRI block.";return false;}
            ++i;
            int sourceId=-1; {std::istringstream vs(lines[i].substr(5));vs>>sourceId;}
            Vertex v; bool gotP=false,gotN=false,gotUv=false;
            auto pit=sourcePositions.find(sourceId);
            if(pit!=sourcePositions.end()){v.position={pit->second.x,pit->second.y,pit->second.z};gotP=true;}
            for(size_t j=i+1;j<lines.size();++j)
            {
                const auto&s=lines[j];
                if(s.rfind("VERT ",0)==0||s.rfind("TRI",0)==0){i=j-1;break;}
                if(s.rfind("OFFSET ",0)==0){V3 p;if(parseTriple(s,p)){v.position={p.x,p.y,p.z};gotP=true;}}
                else if(s.rfind("NORMAL ",0)==0){V3 n;if(parseTriple(s,n)){n=norm(n);v.normal={n.x,n.y,n.z};gotN=true;}}
                else if(s.rfind("UV ",0)==0){V2 t;if(parseUv(s,t)){v.uv={t.x,1.0f-t.y};gotUv=true;}}
                if(gotP&&gotN&&gotUv){i=j;break;}
                if(j+1==lines.size())i=j;
            }
            if(!gotP){error="XMODEL_EXPORT references a vertex with no OFFSET position. Convert this model to OBJ/FBX or use a standard XMODEL_EXPORT dump.";return false;}
            tri[c]=(std::uint32_t)out.vertices.size();out.vertices.push_back(v);
        }
        out.indices.insert(out.indices.end(),{tri[0],tri[1],tri[2]});
    }
    if(out.indices.empty()){error="XMODEL_EXPORT contains no readable TRI face blocks.";return false;}
    out.sourceFormat="XMODEL_EXPORT"; finalize(out); return true;
}
}

bool loadModel(const std::string& path, Mesh& mesh, std::string& error)
{
    mesh={}; error.clear(); mesh.sourcePath=path;
    const auto ext=lower(std::filesystem::path(path).extension().string());
    bool ok=false;
    if(ext==".obj")ok=loadObj(path,mesh,error);
    else if(ext==".fbx")ok=loadAsciiFbx(path,mesh,error);
    else if(ext==".xmodel_export"||ext==".xmodel")ok=loadXModelExport(path,mesh,error);
    else if(ext==".xmodel_bin")ok=loadXModelBin(path,mesh,error);
    else {error="Unsupported model format. Use OBJ, ASCII FBX, XMODEL_EXPORT, or BO3 XMODEL_BIN.";return false;}
    if(ok&&mesh.vertices.size()>2'000'000){error="Model is too large for the previewer (over 2 million vertices).";return false;}
    return ok;
}
}
