/*
 * bulk_extractor_api.cpp:
 * Implements an API for incorporating bulk_extractor in other programs.
 *
 */
#if 0


#include "config.h"
#include "bulk_extractor.h"
#include "bulk_extractor_api.h"
#include "image_process.h"
//#include "be_threadpool.h"
#include "be13_api/aftimer.h"
#include "histogram.h"
#include "dfxml_cpp/src/dfxml_writer.h"
#include "dfxml_cpp/src/hash_t.h"

#include "phase1.h"

#include <ctype.h>
#include <fcntl.h>
#include <set>
#include <setjmp.h>
#include <vector>
#include <queue>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>

/****************************************************************
 *** Here is the bulk_extractor API
 *** It is under development.
 ****************************************************************/

/* The API relies on a special version of the feature recorder set
 * that writes to a callback function instead of a
 */

class callback_feature_recorder_set;

/* callback_feature_recorder_set is
 * a special feature_recorder_set that calls a callback rather than writing to a file.
 * Typically we will instantiate a single object called the 'cfs' for each BEFILE.
 * It creates multiple named callback_feature_recorders, but they all callback through the same
 * callback function using the same set of locks
 */

#if 0
TK: plugin:: had been moved from a global to a scanner_set class, so these need to be changed to create an instance of the class, rather than using a statiatlcaly allocated global.

class callback_feature_recorder_set: public feature_recorder_set {
    callback_feature_recorder_set(const callback_feature_recorder_set &cfs)=delete;
    callback_feature_recorder_set &operator=(const callback_feature_recorder_set &cfs)=delete;
    histogram_defs_t histogram_defs;

public:
    void            *user;
    be_callback_t   *cb;
    mutable std::mutex Mcb;               // mutex for the callback

    virtual void heartbeat() {
        (*cb)(user, BULK_EXTRACTOR_API_CODE_HEARTBEAT,0,0,0,0,0,0,0);
    }

    virtual feature_recorder *create_name_factory(const std::string &name_);
    callback_feature_recorder_set(void *user_, be_callback_t *cb_, const std::string &hash_alg):
        feature_recorder_set(feature_recorder_set::DISABLE_FILE_RECORDERS, hash_alg,
                             feature_recorder_set::NO_INPUT,feature_recorder_set::NO_OUTDIR),
        histogram_defs(),user(user_),cb(cb_),Mcb(){
    }

    virtual void init_cfs(){
        feature_file_names_t feature_file_names;
        plugin::scanners_process_enable_disable_commands();
        plugin::get_scanner_feature_file_names(feature_file_names);
        init(feature_file_names); // creates the feature recorders
        plugin::add_enabled_scanner_histograms_to_feature_recorder_set(*this);
        plugin::scanners_init(*this); // must be done after feature recorders are created
    }

    virtual void write(const std::string &feature_recorder_name,const std::string &str){
        const std::lock_guard<std::mutex> lock(Mcb);
        (*cb)(user,BULK_EXTRACTOR_API_CODE_FEATURE,0,
              feature_recorder_name.c_str(),"",str.c_str(),str.size(),"",0);
    }
    virtual void write0(const std::string &feature_recorder_name,
                        const pos0_t &pos0,const std::string &feature,const std::string &context){
        const std::lock_guard<std::mutex> lock(Mcb);
        (*cb)(user,BULK_EXTRACTOR_API_CODE_FEATURE,0,
              feature_recorder_name.c_str(),pos0.str().c_str(),
              feature.c_str(),feature.size(),context.c_str(),context.size());
    }

    /* The callback function that will be used to dump a histogram line.
     * it will in turn call the callback function
     */
    static int histogram_dump_callback(void *user,const feature_recorder &fr,
                                       const histogram_def &def,
                                       const std::string &str,const uint64_t &count) {
        callback_feature_recorder_set *cfs = (callback_feature_recorder_set *)(user);
        assert(cfs!=0);
        assert(cfs->cb!=0);
        std::string name = fr.name;
        if(def.suffix.size()) name+= "_" + def.suffix;
        return (*cfs->cb)(user,BULK_EXTRACTOR_API_CODE_HISTOGRAM,
                          count,name.c_str(),"",str.c_str(),str.size(),"",0);
    }
};
#endif


#if 0

class callback_feature_recorder: public feature_recorder {
    // neither copying nor assignment are implemented
    callback_feature_recorder(const callback_feature_recorder &cfr)=delete;
    callback_feature_recorder &operator=(const callback_feature_recorder&cfr)=delete;
public:
    be_callback_t *cb;                  // the callback function
    callback_feature_recorder(be_callback_t *cb_,
                              class feature_recorder_set &fs_,const std::string &name_):
        feature_recorder(fs_,name_),cb(cb_){
    }
    virtual std::string carve(const sbuf_t &sbuf,size_t pos,size_t len,
                              const std::string &ext){ // appended to forensic path
        return("");                      // no file created
    }
    virtual void open(){}                // we don't open
    virtual void close(){}               // we don't open
    virtual void flush(){}               // we don't open

    /** write 'feature file' data to the callback */
    virtual void write(const std::string &str){
        dynamic_cast<callback_feature_recorder_set *>(&fs)->write(name,str);
    }
    virtual void write0(const pos0_t &pos0,const std::string &feature,const std::string &context){
        dynamic_cast<callback_feature_recorder_set *>(&fs)->write0(name,pos0,feature,context);
    }
    virtual void write(const pos0_t &pos0,const std::string &feature,const std::string &context){
        feature_recorder::write(pos0,feature,context); // pass up
    }

};


/* create_name_factory must be here, after the feature_recorder class is defined. */
feature_recorder *callback_feature_recorder_set::create_name_factory(const std::string &name_){
    return new callback_feature_recorder(cb,*this,name_);
}




struct BEFILE_t {
    BEFILE_t(void *user,be_callback_t cb):fd(),cfs(user,cb,"md5"),cfg(){};
    int                            fd;
    callback_feature_recorder_set  cfs;
    Phase1::Config   cfg;
};
#endif

typedef struct BEFILE_t BEFILE;
extern "C"
BEFILE *bulk_extractor_open(void *user,be_callback_t cb)
{
    histogram_defs_t histograms;
    scanner_info::scanner_config   s_config; // the bulk extractor config

    s_config.debug       = 0;           // default debug

    plugin::load_scanners(scanners_builtin,s_config);

    BEFILE *bef = new BEFILE_t(user,cb);
    return bef;
}

extern "C" void bulk_extractor_config(BEFILE *bef,uint32_t cmd,const char *name,int64_t arg)
{
    switch(cmd){
    case BEAPI_PROCESS_COMMANDS:
        bef->cfs.init_cfs();
        break;

    case BEAPI_SCANNER_DISABLE:
        plugin::scanners_disable(name);
        break;

    case BEAPI_SCANNER_ENABLE:
        plugin::scanners_enable(name);
        break;

    case BEAPI_FEATURE_DISABLE: {
        feature_recorder &fr = bef->cfs.named_feature_recorder(name);
        if(fr) fr->set_flag(feature_recorder::FLAG_NO_FEATURES);
        break;
    }

    case BEAPI_FEATURE_ENABLE:{
        feature_recorder &fr = bef->cfs.named_feature_recorder(name);
        assert(fr);
        if(fr) fr->unset_flag(feature_recorder::FLAG_NO_FEATURES);
        break;
    }

    case BEAPI_MEMHIST_ENABLE:          // enable memory histograms
        bef->cfs.set_flag(feature_recorder_set::MEM_HISTOGRAM);
        break;

    case BEAPI_MEMHIST_LIMIT:{
        feature_recorder &fr = bef->cfs.named_feature_recorder(name);
        assert(fr);
        fr->set_memhist_limit(arg);
        break;
    }

    case BEAPI_DISABLE_ALL:
        plugin::scanners_disable_all();
        break;

    case BEAPI_FEATURE_LIST: {
        /* Get a list of the feature files and send them to the callback */
        std::vector<std::string> ret;
        bef->cfs.get_feature_file_list(ret);
        for(std::vector<std::string>::const_iterator it = ret.begin();it!=ret.end();it++){
            (*bef->cfs.cb)(bef->cfs.user,BULK_EXTRACTOR_API_CODE_FEATURELIST,0,(*it).c_str(),"","",0,"",0);
        }
        break;
    }
    default:
        assert(0);
    }
}


extern "C"
int bulk_extractor_analyze_buf(BEFILE *bef,uint8_t *buf,size_t buflen)
{
    pos0_t pos0("");
    const sbuf_t sbuf(pos0,buf,buflen,buflen,0,false);
    plugin::process_sbuf(scanner_params(scanner_params::PHASE_SCAN,sbuf,bef->cfs));
    return 0;
}

extern "C"
int bulk_extractor_analyze_dev(BEFILE *bef,const char *fname,float frac,int pagesize)
{
    bool sampling_mode = frac < 1.0; // are we in sampling mode or full-disk mode?

    /* A single-threaded sampling bulk_extractor.
     * It may be better to do this with two threads---one that does the reading (and seeking),
     * the other that doe the analysis.
     *
     * This looks like the code in phase1.cpp.
     */
    Phase1::blocklist_t blocks_to_sample;
    Phase1::blocklist_t::const_iterator si = blocks_to_sample.begin(); // sampling iterator

    image_process *p = image_process::open(fname,false,pagesize,pagesize);
    image_process::iterator it = p->begin(); // get an iterator

    if(sampling_mode){
        Phase1::make_sorted_random_blocklist(&blocks_to_sample, it.max_blocks(),frac);
        si = blocks_to_sample.begin();    // get the new beginning
    }

    while(true){
        if(sampling_mode){             // sampling; position at the next block
            if(si==blocks_to_sample.end()){
                break;
            }
            it.seek_block(*si);
        } else {
            if (it == p->end()){    // end of regular image
                break;
            }
        }

        try {
            auto sbuf = it.sbuf_alloc();
            plugin::process_sbuf(scanner_params(scanner_params::PHASE_SCAN,sbuf,bef->cfs));
        }
        catch (const EOFException &e) {
            break;
        }
        catch (const std::exception &e) {
            (*bef->cfs.cb)(bef->cfs.user,BULK_EXTRACTOR_API_EXCEPTION,0,
                           e.what(),it.get_pos0().str().c_str(),"",0,"",0);
        }
        if(sampling_mode){
            ++si;
        } else {
            ++it;
        }
    }
    return 0;
}

extern "C"
int bulk_extractor_close(BEFILE *bef)
{
    bef->cfs.dump_histograms((void *)&bef->cfs,
                             callback_feature_recorder_set::histogram_dump_callback,0);
    delete bef;
    return 0;
}
#endif
