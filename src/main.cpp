#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef __linux__
#include <sched.h>
#endif

using Clock = std::chrono::steady_clock;
using Cell = std::pair<int,int>;
using Shape = std::vector<Cell>;
static std::atomic<bool> interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free);
static void onSignal(int) { interrupted.store(true,std::memory_order_relaxed); }
static constexpr const char* pending =
"1/2/4 1/4/6 2/5/7 2/5/8 3/5/8 3/6/8 4/6/8 4/7/8 "
"1/2/3/6 1/2/4/6 1/2/4/7 1/3/4/7 2/3/4/7 2/3/5/8 2/3/6/7 2/3/6/8 "
"2/4/5/8 2/4/6/7 2/4/6/8 2/4/7/8 2/5/6/7 2/5/6/8 2/5/7/8 3/4/5/8 "
"3/4/7/8 3/5/6/8 3/5/7/8 3/6/7/8 1/2/3/4/7 1/2/3/5/6 1/2/3/5/7 "
"1/2/3/5/8 1/2/3/6/7 1/2/3/6/8 1/2/4/5/6 1/2/4/5/7 1/2/4/5/8 "
"1/2/4/6/7 1/2/4/6/8 1/2/4/7/8 1/3/4/5/8 1/3/4/6/7 1/3/4/6/8 "
"1/3/4/7/8 1/3/5/6/7 1/3/5/6/8 1/3/5/7/8 1/3/6/7/8 1/4/5/6/7 "
"1/4/5/6/8 1/4/5/7/8 1/4/6/7/8 2/3/4/5/8 2/3/6/7/8 2/5/6/7/8 "
"3/4/5/6/8 3/4/5/7/8 3/4/6/7/8 1/2/3/4/5/8 1/2/3/4/6/7 1/2/3/4/6/8 "
"1/2/3/4/7/8 1/2/3/5/6/7 1/2/3/5/6/8 1/2/3/5/7/8 1/2/3/6/7/8 "
"1/2/4/5/6/7 1/2/4/5/6/8 1/2/4/5/7/8 1/2/4/6/7/8 1/3/4/5/7/8 "
"1/3/5/6/7/8 1/4/5/6/7/8 2/3/4/5/7/8 2/3/5/6/7/8 "
"1/2/3/4/5/7/8 1/2/3/4/6/7/8 1/2/3/5/6/7/8 1/2/4/5/6/7/8";

// Select high-degree centres. Their 3x3 dilation lies in the shape except
// for at most the sum of their missing neighbours. Horizontal/vertical
// dilation adds >=2*rows + 2*columns +4 >=2*ceil(2*sqrt(M))+4 cells.
// For a fixed M, selecting the M highest-degree centres minimizes the deficit.
static int minimumFootprint(const std::array<int,9>& h) {
    int selected=0,missing=0,lines=0,required=0;
    for(int d=8;d>=0;--d)for(int j=0;j<h[d];++j) {
        ++selected;missing+=8-d;
        while(lines*lines<4*selected)++lines;
        required=std::max(required,selected+2*lines+4-missing);
    }
    return required;
}

struct Target {
    int mask=0, k=0, sum=0, maximum=0;
    std::string name;
    explicit Target(const std::string& s) {
        std::stringstream in(s); std::string part;
        while (std::getline(in,part,'/')) {
            if(part.size()!=1 || part[0]<'0' || part[0]>'8') throw std::runtime_error("invalid class: "+s);
            int d=part[0]-'0';
            if(mask&(1<<d)) throw std::runtime_error("duplicate type: "+s);
            mask|=1<<d; ++k; sum+=d; maximum=std::max(maximum,d);
        }
        if(!k || s.back()=='/') throw std::runtime_error("empty class");
        for(int d=0;d<=8;++d) if(mask&(1<<d)) { if(!name.empty())name+='/'; name+=char('0'+d); }
    }
    bool feasible(int m) const {
        int n=m*k;
        if(mask&1) return mask==1 && m==1;
        if((m*sum&1) || m*sum<2*(n-1) || maximum>=n || !(mask&30))return false;
        // Opposite lexicographic extrema are distinct and each has degree <=4.
        if(m*std::popcount(unsigned(mask&30))<2)return false;
        // In each of the four line directions, edges <= N - occupied lines.
        // Row/column and diagonal-coordinate products are both >= N, so each
        // pair of line counts sums to >= ceil(2*sqrt(N)). Hence sum(degrees)
        // <= 8*N - 4*ceil(2*sqrt(N)), with no assumption of a rectangular shape.
        int lines=int(std::sqrt(4*n));while(lines*lines<4*n)++lines;
        if(m*sum>8*n-4*lines)return false;
        // A degree-1 cell has a cardinal support of degree 2, 3 or 4,
        // except for the isolated domino.
        if(n>2 && (mask&2) && !(mask&28))return false;
        std::array<int,9> h{};
        for(int d=0;d<=8;++d)if(mask&(1<<d))h[d]=m;
        return n>=minimumFootprint(h);
    }
    bool matches(const std::array<int,9>& h,int m) const {
        for(int d=0;d<=8;++d) if(h[d]!=((mask&(1<<d))?m:0)) return false;
        return true;
    }
};

// Deliberately independent of incremental search bookkeeping.
static std::array<int,9> validate(const Shape& shape) {
    std::set<Cell> cells(shape.begin(),shape.end());
    if(cells.empty() || cells.size()!=shape.size()) throw std::runtime_error("empty shape or duplicate cells");
    std::array<int,9> h{};
    for(auto [x,y]:cells) {
        int d=0;
        for(int dx=-1;dx<=1;++dx) for(int dy=-1;dy<=1;++dy)
            if(dx||dy) d+=cells.contains({x+dx,y+dy});
        ++h[d];
    }
    std::set<Cell> seen; Shape q{*cells.begin()}; seen.insert(q[0]);
    for(size_t i=0;i<q.size();++i) {
        auto [x,y]=q[i];
        for(auto [dx,dy]:Shape{{1,0},{-1,0},{0,1},{0,-1}}) {
            Cell c{x+dx,y+dy};
            if(cells.contains(c) && seen.insert(c).second) q.push_back(c);
        }
    }
    if(seen.size()!=cells.size()) throw std::runtime_error("shape is not edge-connected");
    return h;
}
static Shape normalized(Shape s) {
    int x=s[0].first,y=s[0].second;
    for(auto c:s) { x=std::min(x,c.first); y=std::min(y,c.second); }
    for(auto& c:s) { c.first-=x;c.second-=y; }
    std::sort(s.begin(),s.end()); return s;
}
static std::string canonical(const Shape& s) {
    Shape best;
    for(int swap=0;swap<2;++swap) for(int sx:{-1,1}) for(int sy:{-1,1}) {
        Shape t; t.reserve(s.size());
        for(auto [x,y]:s) t.emplace_back(sx*(swap?y:x),sy*(swap?x:y));
        t=normalized(std::move(t)); if(best.empty()||t<best) best=std::move(t);
    }
    std::string key;
    for(auto [x,y]:best) key+=std::to_string(x)+","+std::to_string(y)+";";
    return key;
}
struct Rng {
    std::array<uint32_t,4> state;
    explicit Rng(uint64_t seed) {
        for(auto& v:state) {
            uint64_t z=(seed+=0x9e3779b97f4a7c15ULL);
            z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
            z=(z^(z>>27))*0x94d049bb133111ebULL;
            v=uint32_t(z^(z>>31));
        }
        if(!(state[0]|state[1]|state[2]|state[3]))state[0]=1;
    }
    uint32_t next() { // xoshiro128++: 2^128-1 period; SplitMix64 seeded.
        uint32_t result=std::rotl(state[0]+state[3],7)+state[0];
        uint32_t t=state[1]<<9;
        state[2]^=state[0];state[3]^=state[1];state[1]^=state[2];state[0]^=state[3];
        state[2]^=t;state[3]=std::rotl(state[3],11);return result;
    }
    unsigned index(unsigned n) { return static_cast<unsigned>((uint64_t(next())*n)>>32); }
    double unit() { return next()*0x1.0p-32; }
};

// Padded dynamic board. A side of 2*N+3 fits every N-cell connected shape
// translated to contain the initial cell, so there is no 22x22 restriction.
class Annealer {
public:
    int W,S,n,m,length=0,frontLength=0,matched=0;
    std::array<int,8> off;
    std::array<int,4> edge;
    std::vector<uint8_t> occ,degree,edgeDegree,allowed;
    std::vector<int> active,pos,front,frontPos,queue;
    std::vector<uint32_t> seen;
    uint32_t stamp=0;
    std::array<int,9> hist{},goal{};
    std::array<uint8_t,256> safe{};
    const Target& target;
    Rng rng;
    uint64_t moves=0,accepted=0,floods=0;
    Annealer(const Target& t,int each,uint64_t seed):
        W(2*t.k*each+3),S(W*W),n(t.k*each),m(each),
        off{-W-1,-W,-W+1,1,W+1,W,W-1,-1},edge{-W,1,W,-1},
        occ(S),degree(S),edgeDegree(S),allowed(S),active(S),pos(S,-1),
        front(S),frontPos(S,-1),queue(S),seen(S),target(t),rng{seed} {
        for(int y=1;y<W-1;++y) for(int x=1;x<W-1;++x) allowed[y*W+x]=1;
        for(int d=0;d<=8;++d) goal[d]=(t.mask&(1<<d))?m:0;
        for(int mask=0;mask<256;++mask) {
            int targets=mask&0xaa;
            int reached=targets&-targets,prev;
            do {
                prev=reached;
                reached|=((reached<<1)|(reached>>7)|(reached>>1)|((reached&1)<<7))&mask&255;
            } while(prev!=reached);
            safe[mask]=(reached&targets)==targets;
        }
    }
    void sync(int x) {
        int p=frontPos[x];
        if(allowed[x]&&!occ[x]&&edgeDegree[x]) {
            if(p<0) {frontPos[x]=frontLength;front[frontLength++]=x;}
        } else if(p>=0) {
            int last=front[--frontLength];front[p]=last;frontPos[last]=p;frontPos[x]=-1;
        }
    }
    void change(int d,int delta) {
        int before=hist[d];hist[d]+=delta;
        matched += delta>0 ? int(before<goal[d]) : -int(before<=goal[d]);
    }
    void put(int x) {
        occ[x]=1;change(degree[x],1);
        for(int o:off) { int y=x+o,d=degree[y]++; if(occ[y]) {change(d,-1);change(d+1,1);} }
        for(int o:edge) {++edgeDegree[x+o];sync(x+o);} sync(x);
        pos[x]=length;active[length++]=x;
    }
    void take(int x) {
        change(degree[x],-1);occ[x]=0;
        for(int o:off) {int y=x+o,d=degree[y]--;if(occ[y]) {change(d,-1);change(d-1,1);} }
        for(int o:edge) {--edgeDegree[x+o];sync(x+o);} sync(x);
        int p=pos[x],last=active[--length];active[p]=last;pos[last]=p;pos[x]=-1;
    }
    int maskAt(int x) const {
        int mask=0;for(int i=0;i<8;++i) mask|=int(occ[x+off[i]])<<i;return mask;
    }
    bool reconnect(int x,int mask) {
        ++floods;
        int remaining=0,first=0;
        for(int i=0;i<4;++i) if(mask&(1<<(2*i+1))) {first=x+edge[i];++remaining;}
        if(remaining<=1)return true;
        if(++stamp==0) { std::fill(seen.begin(),seen.end(),0);stamp=1; }
        int head=0,tail=1;queue[0]=first;seen[first]=stamp;
        while(head<tail) {
            int z=queue[head++];
            for(int i=0;i<4;++i) if(z==x+edge[i] && (mask&(1<<(2*i+1)))) --remaining;
            if(!remaining)return true;
            for(int o:edge) {int y=z+o;if(occ[y]&&seen[y]!=stamp) {seen[y]=stamp;queue[tail++]=y;}}
        }
        return false;
    }
    void restart() {
        std::fill(occ.begin(),occ.end(),0);std::fill(degree.begin(),degree.end(),0);
        std::fill(edgeDegree.begin(),edgeDegree.end(),0);std::fill(pos.begin(),pos.end(),-1);
        std::fill(frontPos.begin(),frontPos.end(),-1);hist.fill(0);matched=length=frontLength=0;
        put((W/2)*W+W/2);
        while(length<n) {int x=front[rng.index(frontLength)];if(rng.index(4)<edgeDegree[x])put(x);}
    }
    static bool adjacent8(int diff,int width) {
        return (diff!=0 && diff>=-1 && diff<=1) ||
               (diff>=width-1 && diff<=width+1) ||
               (diff>=-width-1 && diff<=-width+1);
    }
    int predict(int removed,int added) const {
        std::array<int,9> delta{};
        --delta[degree[removed]];
        int diff=added-removed;
        bool overlap=unsigned(diff+2)<=4 || unsigned(diff-W+2)<=4 ||
            unsigned(diff+W+2)<=4 || unsigned(diff-2*W+2)<=4 || unsigned(diff+2*W+2)<=4;
        if(!overlap) {
            ++delta[degree[added]];
            for(int o:off) {int y=removed+o;if(occ[y]) {--delta[degree[y]];++delta[degree[y]-1];}}
            for(int o:off) {int y=added+o;if(occ[y]) {--delta[degree[y]];++delta[degree[y]+1];}}
        } else {
            ++delta[degree[added]-int(adjacent8(diff,W))];
            // Common neighbours retain their degree; process neither transfer.
            for(int o:off) {
                int y=removed+o;
                if(occ[y]&&!adjacent8(y-added,W)) {--delta[degree[y]];++delta[degree[y]-1];}
            }
            for(int o:off) {
                int y=added+o;
                if(occ[y]&&y!=removed&&!adjacent8(y-removed,W)) {--delta[degree[y]];++delta[degree[y]+1];}
            }
        }
        int next=matched;
        unsigned mask=target.mask;
        while(mask) {
            int d=std::countr_zero(mask);mask&=mask-1;
            next+=std::min(hist[d]+delta[d],goal[d])-std::min(hist[d],goal[d]);
        }
        return next;
    }
    void mutate(const std::array<double,18>& accept) {
        ++moves;if(n<2)return;
        int removed=active[rng.index(n)],added=-1;
        for(int tries=0;tries<8;++tries) {
            uint32_t draw=rng.next();
            int c=front[(uint64_t(draw)*frontLength)>>32];if((draw&3)>=edgeDegree[c])continue;
            bool adjacent=false;for(int o:edge)adjacent|=(c==removed+o);
            if(edgeDegree[c]>int(adjacent)) {added=c;break;}
        }
        if(added<0)return;
        // Reject on a read-only local histogram delta before touching frontier,
        // occupancy or degree caches. Most cold proposals need no transaction.
        int next=predict(removed,added),delta=matched-next;
        if(delta>0 && rng.unit()>=accept[delta])return;
        int mask=maskAt(removed);
        take(removed);put(added);
        if(safe[mask]||reconnect(removed,mask)) ++accepted;
        else {take(added);put(removed);}
    }
    Shape shape() const {
        Shape s;s.reserve(n);for(int i=0;i<length;++i)s.emplace_back(active[i]%W,active[i]/W);
        return normalized(std::move(s));
    }
};

// Exact mode caps N at 24: the frontier has at most 2*N+2 cells.
// Fixed stack storage removes allocations in every enumeration recursion.
struct Candidates {
    std::array<int,64> data{};
    int length=0;
    Candidates()=default;
    Candidates(std::initializer_list<int> init) {for(int x:init)push_back(x);}
    void push_back(int x) {if(length==64)throw std::runtime_error("exact frontier capacity exceeded");data[length++]=x;}
    int back() const {return data[length-1];}
    void pop_back() {--length;}
    bool empty() const {return length==0;}
    auto begin() {return data.begin();}
    auto end() {return data.begin()+length;}
};

// Redelmeier enumeration of fixed polyominoes, once per translation.
// Each recursion owns its forbidden additions, retaining ancestors' exclusions.
class Exact {
    int W,S,root,maxN;
    std::array<int,4> edge;
    std::array<int,8> off;
    std::vector<uint8_t> occ,degree,forbidden;
    std::vector<int> cells;
    std::array<int,9> hist{};
    void put(int x) {
        occ[x]=1;++hist[degree[x]];cells.push_back(x);
        for(int o:off) {int y=x+o,d=degree[y]++;if(occ[y]) {--hist[d];++hist[d+1];}}
    }
    void take(int x) {
        --hist[degree[x]];occ[x]=0;cells.pop_back();
        for(int o:off) {int y=x+o,d=degree[y]--;if(occ[y]) {--hist[d];++hist[d-1];}}
    }
    bool valid(int x) const {return x/W>=1 && (x/W>1||x>=root) && x/W<W-1 && x%W>0 && x%W<W-1;}
public:
    uint64_t nodes=0;
    bool complete=true;
    std::vector<uint64_t> counts;
    std::function<bool()> stop=[] {return false;};
    int splitDepth=0;
    int degreeLimit=8;
    std::function<bool(const std::array<int,9>&,int)> viable;
    std::function<void(const Exact&,Candidates)> enqueue;
    std::function<void(const std::vector<int>&,int,const std::array<int,9>&)> visit;
    explicit Exact(int n):W(2*n+3),S(W*W),root(W+n+1),maxN(n),edge{-W,1,W,-1},
        off{-W-1,-W,-W+1,1,W+1,W,W-1,-1},occ(S),degree(S),forbidden(S),counts(n+1) {}
    void recurse(Candidates untried) {
        Candidates excluded;
        while(!untried.empty()) {
            if((nodes&4095)==0 && stop()) {complete=false;break;}
            int x=untried.back();untried.pop_back();put(x);++nodes;++counts[cells.size()];
            if(visit) visit(cells,W,hist);
            bool extend=true;
            for(int d=degreeLimit+1;d<=8;++d) if(hist[d])extend=false;
            if(extend && int(cells.size())<maxN && (!viable || viable(hist,int(cells.size())))) {
                auto next=untried;
                for(int o:edge) {
                    int y=x+o;
                    if(valid(y)&&!occ[y]&&!forbidden[y]&&std::find(next.begin(),next.end(),y)==next.end()) next.push_back(y);
                }
                if(enqueue && int(cells.size())==splitDepth) enqueue(*this,std::move(next));
                else recurse(std::move(next));
            }
            take(x);forbidden[x]=1;excluded.push_back(x);
            if(!complete)break;
        }
        for(int x:excluded)forbidden[x]=0;
    }
    void run() {recurse({root});}
};

static int defaultThreads() {
    unsigned count=std::max(1u,std::thread::hardware_concurrency());
#ifdef __linux__
    cpu_set_t cpus;
    if(sched_getaffinity(0,sizeof(cpus),&cpus)==0)count=std::max(1,CPU_COUNT(&cpus));
#endif
    return int(std::min(8u,count));
}

struct Options {
    std::string mode="search",strategy="mixed",output,verify;
    std::vector<Target> targets;
    int minEach=2,maxEach=8,threads=defaultThreads(),maxCells=12;
    uint64_t perThread=0,movesPerThread=0,solutions=1,seed=1;
    double seconds=60;
    bool selfTest=false,completionPruning=true;
};
static uint64_t integer(const std::string& s) {
    size_t used=0;if(s.empty()||s[0]=='-')throw std::runtime_error("expected nonnegative integer: "+s);
    auto n=std::stoull(s,&used);if(used!=s.size())throw std::runtime_error("invalid integer: "+s);return n;
}
static void help() {
    std::cout<<R"(polyomino-search [options]
  --mode search|exact       Annealing (default) or exhaustive enumeration
  --target 1/2/4            Repeat for multiple targets; default: all 79 supplied
  --min-each N --max-each N Equal multiplicity range (default 2..8, maximum 64)
  --threads N              Worker count in either mode (default min(available CPUs,8), max 256)
  --seconds N              Wall time limit, 0 = unlimited (default 60)
  --solutions N            Unique witnesses per class, 0 = unlimited (default 1)
  --many                   Alias for --solutions 0
  --per-thread N           Stop each search worker after N novel witnesses (0 unlimited)
  --moves-per-thread N     Cap mutation proposals per worker exactly (0 unlimited)
  --strategy mixed|anneal|cold  Search cooling portfolio (default mixed)
  --seed N                 Reproducible single-worker PRNG seed (default 1)
  --max-cells N            Exact enumeration ceiling (default 12, maximum 24)
  --no-completion-pruning  Disable exact partial-degree capacity pruning
  --output FILE            JSON Lines results (default stdout); progress on stderr
  --verify FILE            Validate whitespace x y coordinate pairs, # comments allowed
  --self-test              Incremental invariants and known enumeration counts
  --help                   This text
Exact mode covers the infinite lattice up to --max-cells, not all sizes.
Search exhaustion/timeouts are UNKNOWN, never impossibility proofs.
Ctrl-C stops gracefully; interrupted enumeration cannot prove absence.
)";
}
static Options parse(int argc,char** argv) {
    Options o;
    for(int i=1;i<argc;++i) {
        std::string a=argv[i];
        auto arg=[&] {if(++i==argc)throw std::runtime_error("missing value for "+a);return std::string(argv[i]);};
        auto bounded=[&](uint64_t max) {auto n=integer(arg());if(n>max)throw std::runtime_error("value too large for "+a);return int(n);};
        if(a=="--help") {help();std::exit(0);}
        else if(a=="--mode")o.mode=arg();
        else if(a=="--strategy")o.strategy=arg();
        else if(a=="--target")o.targets.emplace_back(arg());
        else if(a=="--min-each")o.minEach=bounded(64);
        else if(a=="--max-each")o.maxEach=bounded(64);
        else if(a=="--threads")o.threads=bounded(256);
        else if(a=="--max-cells")o.maxCells=bounded(24);
        else if(a=="--seconds") {auto s=arg();size_t used;o.seconds=std::stod(s,&used);if(used!=s.size()||!std::isfinite(o.seconds)||o.seconds<0)throw std::runtime_error("invalid time limit");}
        else if(a=="--per-thread")o.perThread=integer(arg());
        else if(a=="--moves-per-thread")o.movesPerThread=integer(arg());
        else if(a=="--solutions")o.solutions=integer(arg());
        else if(a=="--many")o.solutions=0;
        else if(a=="--seed")o.seed=integer(arg());
        else if(a=="--output")o.output=arg();
        else if(a=="--verify")o.verify=arg();
        else if(a=="--self-test")o.selfTest=true;
        else if(a=="--no-completion-pruning")o.completionPruning=false;
        else throw std::runtime_error("unknown option: "+a);
    }
    if(o.mode!="search"&&o.mode!="exact")throw std::runtime_error("mode must be search or exact");
    if(o.strategy!="mixed"&&o.strategy!="anneal"&&o.strategy!="cold")throw std::runtime_error("strategy must be mixed, anneal or cold");
    if(!o.minEach||o.minEach>o.maxEach||!o.threads||!o.maxCells)throw std::runtime_error("invalid bounds");
    if(o.targets.empty()) {std::stringstream in(pending);std::string s;while(in>>s)o.targets.emplace_back(s);}
    std::set<int> masks;for(auto& t:o.targets)if(!masks.insert(t.mask).second)throw std::runtime_error("duplicate target");
    return o;
}

struct Results {
    const Options& options;
    std::ostream& out;
    Clock::time_point start=Clock::now();
    std::mutex mutex;
    std::vector<std::unordered_set<std::string>> keys;
    std::vector<std::atomic<uint64_t>> found;
    std::atomic<uint64_t> moves{0},restarts{0},accepted{0},floods{0};
    std::atomic<bool> cancelled{false};
    Results(const Options& o,std::ostream& stream):options(o),out(stream),keys(o.targets.size()),found(o.targets.size()) {}
    bool done(size_t i) const {return options.solutions && found[i].load(std::memory_order_relaxed)>=options.solutions;}
    bool expired() const {return cancelled.load(std::memory_order_relaxed) || interrupted || (options.seconds>0 && std::chrono::duration<double>(Clock::now()-start).count()>=options.seconds);}
    bool publish(size_t i,int m,int worker,const Shape& shape) {
        if(!options.targets[i].matches(validate(shape),m))throw std::runtime_error("internal error: invalid search witness");
        auto key=canonical(shape);auto cells=normalized(shape);
        std::lock_guard<std::mutex> lock(mutex);
        if(done(i)||!keys[i].insert(std::move(key)).second)return false;
        ++found[i];
        out<<"{\"event\":\"found\",\"target\":\""<<options.targets[i].name<<"\",\"each\":"<<m<<",\"worker\":"<<worker<<",\"cells\":[";
        for(size_t j=0;j<cells.size();++j) {if(j)out<<',';out<<'['<<cells[j].first<<','<<cells[j].second<<']';}
        out<<"]}\n";out.flush();return true;
    }
};
static void searchWorker(int id,Results& results) {
    const auto& o=results.options;
    Rng rng{o.seed+0xd1b54a32d192ed03ULL*(id+1)};
    std::vector<std::pair<size_t,int>> jobs;
    for(size_t i=0;i<o.targets.size();++i)for(int m=o.minEach;m<=o.maxEach;++m)
        if(o.targets[i].feasible(m))jobs.emplace_back(i,m);
    // Preserve a full-temperature exploration stream alongside colder, shorter
    // restarts. Single-worker mixed mode alternates schedules between sweeps.
    uint64_t quota=0,attempts=0,sweep=0;
    auto budgetReached=[&] {
        return (o.perThread&&quota>=o.perThread) ||
               (o.movesPerThread&&attempts>=o.movesPerThread);
    };
    while(!results.expired()&&!budgetReached()) {
        bool remaining=false;
        bool cold=o.strategy=="cold" ||
            (o.strategy=="mixed" && ((o.threads==1?sweep:uint64_t(id))&1));
        for(size_t j=jobs.size();j>1;--j)std::swap(jobs[j-1],jobs[rng.index(j)]);
        for(auto [i,m]:jobs) {
            if(results.expired()||budgetReached())return;
            if(results.done(i))continue;
            remaining=true;
            Annealer a(o.targets[i],m,rng.next());
            // Reuse allocated arrays across restarts at this size.
            for(int restart=0;restart<3 && !results.done(i) && !results.expired() && !budgetReached();++restart) {
                a.restart();++results.restarts;
                int limit=5500+140*a.n;
                if(cold)limit/=2;
                std::array<double,18> accept{};
                for(int phase=0;phase<limit&&!budgetReached();++phase) {
                    if(a.matched==a.n) {
                        bool novel=results.publish(i,m,id,a.shape());
                        quota+=novel;
                        if(novel||results.done(i))break;
                        // A duplicate immediate solution must still mutate, so
                        // even singleton/domino jobs consume a proposal budget.
                    }
                    if((phase&255)==0) {
                        if(results.expired()||results.done(i))break;
                        double frac=1-double(phase)/limit;
                        double temperature=cold ? 0.15+1.5*frac*frac : 0.22+3.4*frac*frac;
                        for(int d=1;d<18;++d)accept[d]=std::exp(-2*d/temperature);
                    }
                    a.mutate(accept);++attempts;
                }
                // Includes a solution first reached by the final mutation.
                if(a.matched==a.n && !results.done(i) && (!o.perThread||quota<o.perThread))
                    quota+=results.publish(i,m,id,a.shape());
            }
            results.moves+=a.moves;results.accepted+=a.accepted;results.floods+=a.floods;
        }
        if(!remaining)return;
        ++sweep;
    }
}
static void selfTest() {
    const std::vector<uint64_t> expected{0,1,2,6,19,63,216,760,2725};
    Exact e(8);e.run();if(e.counts!=expected)throw std::runtime_error("fixed polyomino counts mismatch");
    Exact geometry(10);
    geometry.visit=[](const std::vector<int>& cells,int,const std::array<int,9>& h) {
        if(minimumFootprint(h)>int(cells.size()))throw std::runtime_error("footprint bound excludes a shape");
        bool equal=true;int each=0;std::string name;
        for(int d=0;d<=8;++d)if(h[d]) {
            if(each && h[d]!=each)equal=false;
            each=h[d];if(!name.empty())name+='/';name+=char('0'+d);
        }
        if(equal&&!Target(name).feasible(each))throw std::runtime_error("necessary condition excludes a balanced shape");
    };
    geometry.run();
    // Check the full histogram and frontier against independent reconstruction,
    // including accepted/rejected swaps and connectivity after each proposal.
    for(const std::string t:{"1/2/4","2/5/8","1/2/3/4/5/6/7/8"}) {
        Target target(t);Annealer a(target,4,123);a.restart();
        std::array<double,18> accept{};accept.fill(0.65);
        for(int step=0;step<6000;++step) {
            a.mutate(accept);
            if(step%13)continue;
            int removed=a.active[a.rng.index(a.n)],added=a.front[a.rng.index(a.frontLength)];
            int predicted=a.predict(removed,added),old=a.matched;
            a.take(removed);a.put(added);
            if(a.matched!=predicted)throw std::runtime_error("proposal score prediction mismatch");
            a.take(added);a.put(removed);
            if(a.matched!=old)throw std::runtime_error("proposal rollback mismatch");
            if(validate(a.shape())!=a.hist)throw std::runtime_error("histogram/connectivity mismatch");
            int matched=0;for(int d=0;d<=8;++d)matched+=std::min(a.hist[d],a.goal[d]);
            if(matched!=a.matched)throw std::runtime_error("incremental score mismatch");
            std::set<int> frontier;
            for(int x=0;x<a.S;++x) {
                int deg=0,ed=0;
                if(a.allowed[x]) {
                    for(int off:a.off)deg+=a.occ[x+off];
                    for(int off:a.edge)ed+=a.occ[x+off];
                    if(deg!=a.degree[x]||ed!=a.edgeDegree[x])throw std::runtime_error("degree cache mismatch");
                    if(!a.occ[x]&&ed)frontier.insert(x);
                }
            }
            std::set<int> actual(a.front.begin(),a.front.begin()+a.frontLength);
            if(actual!=frontier||int(actual.size())!=a.frontLength)throw std::runtime_error("frontier mismatch");
        }
    }
    std::cout<<"Self-test passed: fixed polyomino counts through 8 cells; geometric bounds through 10 cells; 18000 mutation proposals.\n";
}
static int verifyFile(const std::string& path) {
    std::ifstream file(path);if(!file)throw std::runtime_error("cannot open shape file");
    Shape s;std::string line;
    while(std::getline(file,line)) {
        line=line.substr(0,line.find('#'));std::stringstream in(line);int x,y;std::string extra;
        if(!(in>>x)) {in.clear();in>>extra;if(!extra.empty())throw std::runtime_error("invalid coordinate line");continue;}
        if(!(in>>y)||(in>>extra)||std::abs(int64_t(x))>1000000||std::abs(int64_t(y))>1000000)throw std::runtime_error("invalid coordinate pair");
        s.emplace_back(x,y);
    }
    auto h=validate(s);int each=0;std::string target;
    for(int d=0;d<=8;++d)if(h[d]) {
        if(each&&each!=h[d])throw std::runtime_error("unequal neighbour-type counts");
        each=h[d];if(!target.empty())target+='/';target+=char('0'+d);
    }
    std::cout<<"VALID target="<<target<<" each="<<each<<" cells="<<s.size()<<'\n';return 0;
}
int main(int argc,char** argv) {
    try {
        auto o=parse(argc,argv);
        if(o.selfTest) {selfTest();return 0;}
        if(!o.verify.empty())return verifyFile(o.verify);
        std::signal(SIGINT,onSignal);std::signal(SIGTERM,onSignal);
        std::ofstream file;if(!o.output.empty()) {file.open(o.output);if(!file)throw std::runtime_error("cannot open output file");}
        Results r(o,o.output.empty()?std::cout:file);
        r.out.exceptions(std::ios::badbit|std::ios::failbit);
        std::vector<bool> feasible(o.targets.size(),false);
        for(size_t i=0;i<o.targets.size();++i)for(int m=o.minEach;m<=o.maxEach;++m)feasible[i]=feasible[i]||o.targets[i].feasible(m);
        bool exhaustive=false;
        uint64_t nodes=0;
        if(o.mode=="search") {
            std::cerr<<"Annealing: "<<o.targets.size()<<" classes, "<<o.threads<<" workers; multiplicity "<<o.minEach<<".."<<o.maxEach<<".\n";
            std::vector<std::thread> workers;
            std::mutex errorMutex;std::exception_ptr failure;
            try {
                for(int id=0;id<o.threads;++id)workers.emplace_back([&,id] {
                    try {searchWorker(id,r);}catch(...) {r.cancelled=true;std::lock_guard<std::mutex> lock(errorMutex);failure=std::current_exception();}
                });
            } catch(...) {
                r.cancelled=true;for(auto& worker:workers)worker.join();throw;
            }
            for(auto& worker:workers)worker.join();
            if(failure)std::rethrow_exception(failure);
        } else {
            // One enumeration serves every class and every requested multiplicity.
            // Capacity pruning retains a branch if any requested plan can fit.
            int ceiling=0;
            for(auto& t:o.targets)for(int m=o.minEach;m<=o.maxEach;++m)
                if(t.feasible(m)&&m*t.k<=o.maxCells)ceiling=std::max(ceiling,m*t.k);
            if(ceiling) {
                std::cerr<<"Exact fixed-polyomino enumeration through "<<ceiling<<" cells (one shared traversal).\n";
                Exact e(ceiling);e.stop=[&]{return r.expired();};
                std::array<int,512> lookup;lookup.fill(-1);
                int maxDegree=0;
                for(size_t i=0;i<o.targets.size();++i) {
                    lookup[o.targets[i].mask]=int(i);
                    maxDegree=std::max(maxDegree,o.targets[i].maximum);
                }
                e.degreeLimit=maxDegree;
                struct DegreePlan {int n,m;std::vector<int> types;};
                std::vector<DegreePlan> plans;
                for(const auto& t:o.targets)for(int m=o.minEach;m<=o.maxEach;++m) {
                    if(!t.feasible(m)||m*t.k>o.maxCells)continue;
                    DegreePlan p{m*t.k,m,{}};
                    for(int d=0;d<=8;++d)if(t.mask&(1<<d))p.types.push_back(d);
                    plans.push_back(std::move(p));
                }
                std::sort(plans.begin(),plans.end(),[](const DegreePlan& a,const DegreePlan& b){return a.n>b.n;});
                if(o.completionPruning)e.viable=[&](const std::array<int,9>& h,int n) {
                    // A current degree d can only become d..d+r after r additions.
                    // Assign sorted existing vertices to the smallest available
                    // final-degree slot in that interval. Ordered endpoints make
                    // this greedy matching exact for the relaxed capacity test.
                    // Reject a branch only if EVERY requested final plan fails.
                    for(const auto& p:plans) {
                        if(p.n<=n)continue; // Current-size witnesses were visited.
                        int r=p.n-n,idx=0,capacity=p.m;bool possible=true;
                        for(int d=0;d<=8&&possible;++d) {
                            int left=h[d];
                            while(left) {
                                while(idx<int(p.types.size())&&p.types[idx]<d) {++idx;capacity=p.m;}
                                if(idx==int(p.types.size())||p.types[idx]>d+r) {possible=false;break;}
                                int take=std::min(left,capacity);left-=take;capacity-=take;
                                if(!capacity) {++idx;capacity=p.m;}
                            }
                        }
                        if(possible)return true;
                    }
                    return false;
                };
                e.visit=[&](const std::vector<int>& cells,int width,const std::array<int,9>& h) {
                    int mask=0,each=0;
                    for(int d=0;d<=8;++d) if(h[d]) {
                        if(each&&h[d]!=each)return;
                        each=h[d];mask|=1<<d;
                    }
                    int i=lookup[mask];
                    if(i<0||each<o.minEach||each>o.maxEach||r.done(i))return;
                    Shape shape;shape.reserve(cells.size());
                    for(int c:cells)shape.emplace_back(c%width,c/width);
                    r.publish(i,each,0,shape);
                };
                // Partition the Redelmeier traversal with its exclusion state
                // intact. Prefixes are visited once; suffix tasks are disjoint.
                std::vector<std::pair<Exact,Candidates>> tasks;
                if(o.threads>1 && ceiling>4) {
                    e.splitDepth=4;
                    e.enqueue=[&](const Exact& state,Candidates next) {
                        tasks.emplace_back(state,std::move(next));
                        auto& copy=tasks.back().first;
                        copy.enqueue={};copy.splitDepth=0;copy.nodes=0;
                        std::fill(copy.counts.begin(),copy.counts.end(),0);
                    };
                }
                e.run();
                std::atomic<size_t> cursor{0};std::atomic<uint64_t> suffixNodes{0};
                std::atomic<bool> complete{e.complete};
                std::mutex errorMutex;std::exception_ptr failure;
                std::vector<std::thread> workers;
                try {
                  for(int id=0;id<o.threads && !tasks.empty();++id)workers.emplace_back([&] {
                    try {
                        for(;;) {
                            size_t i=cursor.fetch_add(1);
                            if(i>=tasks.size())break;
                            if(r.expired()) {complete=false;break;}
                            auto& [state,next]=tasks[i];state.recurse(std::move(next));
                            suffixNodes+=state.nodes;
                            if(!state.complete)complete=false;
                        }
                    } catch(...) {r.cancelled=true;std::lock_guard<std::mutex> lock(errorMutex);failure=std::current_exception();}
                  });
                } catch(...) {
                    r.cancelled=true;for(auto& worker:workers)worker.join();throw;
                }
                for(auto& worker:workers)worker.join();
                if(failure)std::rethrow_exception(failure);
                exhaustive=complete;nodes=e.nodes+suffixNodes;

            } else exhaustive=true;
        }
        double seconds=std::chrono::duration<double>(Clock::now()-r.start).count();
        for(size_t i=0;i<o.targets.size();++i) {
            std::string status=r.found[i]?"FOUND":!feasible[i]?"IMPOSSIBLE_IN_MULTIPLICITY_RANGE":
                (o.mode=="exact"&&exhaustive)?"IMPOSSIBLE_WITHIN_CELL_LIMIT":"UNKNOWN";
            r.out<<"{\"event\":\"result\",\"target\":\""<<o.targets[i].name<<"\",\"status\":\""<<status
                 <<"\",\"witnesses\":"<<r.found[i]<<",\"min_each\":"<<o.minEach<<",\"max_each\":"<<o.maxEach;
            if(o.mode=="exact")r.out<<",\"max_cells\":"<<o.maxCells<<",\"enumeration_complete\":"<<(exhaustive?"true":"false");
            r.out<<"}\n";
        }
        r.out<<"{\"event\":\"stats\",\"seconds\":"<<seconds<<",\"moves\":"<<r.moves<<",\"accepted\":"<<r.accepted
             <<",\"connectivity_floods\":"<<r.floods<<",\"restarts\":"<<r.restarts<<",\"exact_nodes\":"<<nodes<<"}\n";
        r.out.flush();
        std::cerr<<"Completed in "<<seconds<<"s; "<<r.moves<<" proposals ("<<uint64_t(r.moves/std::max(seconds,1e-9))<<"/s), "<<nodes<<" exact nodes.\n";
        return interrupted?130:0;
    } catch(const std::exception& e) {std::cerr<<"error: "<<e.what()<<'\n';return 1;}
}
