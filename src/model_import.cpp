#include "model_import.h"

#include <algorithm>
#include <cmath>
#include <cctype>
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
    else {error="Unsupported model format. Use OBJ, ASCII FBX, or text XMODEL_EXPORT.";return false;}
    if(ok&&mesh.vertices.size()>2'000'000){error="Model is too large for the previewer (over 2 million vertices).";return false;}
    return ok;
}
}
