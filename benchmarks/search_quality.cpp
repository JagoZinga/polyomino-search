#define main application_main
#include "../src/main.cpp"
#undef main

// Restore a connected board, retaining PRNG state.
void restore(Annealer& a, const std::vector<int>& cells) {
    while(a.length) a.take(a.active[a.length-1]);
    // Put does not require prefix connectivity; final restored shape is connected.
    for(int c:cells)a.put(c);
}
int main(int argc,char** argv) {
    std::string target=argc>1?argv[1]:"3/5/6/8";
    int each=argc>2?std::stoi(argv[2]):4;
    int strategy=argc>3?std::stoi(argv[3]):0;
    int seeds=argc>4?std::stoi(argv[4]):30;
    int budget=argc>5?std::stoi(argv[5]):300000;
    Target t(target);int hits=0;uint64_t total=0;int sumBest=0;
    auto start=Clock::now();
    for(int seed=1;seed<=seeds;++seed) {
        Annealer a(t,each,seed);std::vector<int> best;
        int bestScore=-1,used=0;bool success=false;
        while(used<budget&&!success) {
            if(strategy==0||strategy==3||best.empty()||(strategy==1&&a.rng.index(4)==0))a.restart();
            else restore(a,best);
            int limit=5500+140*a.n;
            if(strategy>=2)limit/=2;
            std::array<double,18> accept{};
            for(int phase=0;phase<limit&&used<budget;++phase) {
                if(a.matched>bestScore) {bestScore=a.matched;best.assign(a.active.begin(),a.active.begin()+a.length);}
                if(a.matched==a.n) {success=true;break;}
                if((phase&255)==0) {
                    double f=1.-double(phase)/limit;
                    double temp=.22+3.4*f*f;
                    if(strategy>=2&&!best.empty())temp=.15+1.5*f*f;
                    for(int d=1;d<18;++d)accept[d]=std::exp(-2*d/temp);
                }
                a.mutate(accept);++used;
            }
        }
        if(a.matched==a.n)success=true;
        if(success&&!t.matches(validate(a.shape()),each))throw std::runtime_error("invalid benchmark witness");
        hits+=success;total+=used;sumBest+=bestScore;
    }
    std::cout<<target<<" each="<<each<<" strategy="<<strategy<<" hits="<<hits<<"/"<<seeds<<" moves="<<total<<" best="<<sumBest<<" seconds="<<std::chrono::duration<double>(Clock::now()-start).count()<<"\n";
}
