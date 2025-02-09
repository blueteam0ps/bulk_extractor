// https://github.com/catchorg/Catch2/blob/master/docs/tutorial.md#top
#define CATCH_CONFIG_CONSOLE_WIDTH 120
#define DO_NOT_USE_WMAIN

#include "config.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <filesystem>
#include <cstdio>
#include <stdexcept>
#include <unistd.h>
#include <string>
#include <string_view>
#include <sstream>

#include "be13_api/catch.hpp"

#ifdef HAVE_MACH_O_DYLD_H
#include "mach-o/dyld.h"         // Needed for _NSGetExecutablePath
#endif

#include "dfxml_cpp/src/dfxml_writer.h"
#include "be13_api/path_printer.h"
#include "be13_api/scanner_set.h"
#include "be13_api/utils.h"             // needs config.h

#include "bulk_extractor.h"
#include "base64_forensic.h"
#include "bulk_extractor_restarter.h"
#include "bulk_extractor_scanners.h"
#include "exif_reader.h"
#include "image_process.h"
#include "jpeg_validator.h"
#include "phase1.h"
#include "sbuf_decompress.h"
#include "scan_aes.h"
#include "scan_base64.h"
#include "scan_email.h"
#include "scan_msxml.h"
#include "scan_net.h"
#include "scan_pdf.h"
#include "scan_vcard.h"
#include "scan_wordlist.h"

#include "test_be.h"

struct Check {
    Check(const Check &ck):fname(ck.fname),feature(ck.feature){};
    Check &operator=(const Check &ck) = delete;

    Check(std::string fname_, Feature feature_):
        fname(fname_), feature(feature_) {};
    std::string fname;
    Feature feature;                    // defined in be13_api/feature_recorder.h
};

TEST_CASE("test_validate", "[phase1]" ) {
    scanner_config sc;

    sc.outdir = NamedTemporaryDirectory();
    sc.enable_all_scanners();
    feature_recorder_set::flags_t frs_flags;
    frs_flags.pedantic = true;          // for testing

    auto *xreport = new dfxml_writer(sc.outdir / "report.xml", false);

    scanner_set ss(sc, frs_flags, xreport);
    ss.add_scanners(scanners_builtin);
    ss.apply_scanner_commands();
    ss.phase_scan();
    ss.shutdown();
    delete xreport;
}


bool feature_match(const Check &exp, const std::string &line)
{
    auto words = split(line, '\t');
    if (words.size() <2 || words.size() > 3) return false;

    if (debug) std::cerr << "check line=" << line << std::endl;

    std::string pos = exp.feature.pos.str();
    if ( pos.size() > 2 ){
        if (ends_with(pos,"-0")) {
            pos.resize(pos.size()-2);
        }
        if (ends_with(pos,"|0")) {
            pos.resize(pos.size()-2);
        }
    }

    if ( pos0_t(words[0]) != exp.feature.pos ){
        if (debug) std::cerr << "  pos " << exp.feature.pos.str() << " does not match '" << words[0] << "'" << std::endl;
        return false;
    }

    if ( words[1] != exp.feature.feature ){
        if (debug)std::cerr << "  feature '" << exp.feature.feature << "' does not match feature '" << words[1] << "'" << std::endl;
        return false;
    }

    std::string ctx = exp.feature.context;
    if (words.size()==2) return ctx=="";

    if ( (ctx=="") || (ctx==words[2]) )  return true;

    if (debug) std::cerr << "  context '" << ctx << "' (len=" << ctx.size() << ") "
                         << "does not match context '" << words[2] << "' (" << words[2].size() << ")\n";

    if ( ends_with(ctx, "*") ) {
        ctx.resize(ctx.size()-1 );
        if (starts_with(words[2], ctx )){
            return true;
        }
        if (debug) std::cerr << "  context did not start with '" << ctx << "'\n";
    } else {
        if (debug) std::cerr << "  context does not end with *\n";
    }

    return false;
}


/*
 * Run all of the built-in scanners on a specific image, look for the given features, and return the directory.
 * These are run single-threading for ease of debugging.
 */
std::filesystem::path validate(std::string image_fname, std::vector<Check> &expected, bool recurse=true, size_t offset=0)
{
    int start_sbuf_count = sbuf_t::sbuf_count;

    debug = getenv_debug("DEBUG");
    bulk_extractor_set_debug();           // Set from getenv
    sbuf_t::debug_range_exception = true; // make sure this is explicitly set
    scanner_config sc;

    sc.outdir           = NamedTemporaryDirectory();
    sc.enable_all_scanners();
    sc.allow_recurse    = recurse;

    std::cerr << "## image_fname: " << image_fname << " outdir: " << sc.outdir << std::endl;

    if (offset==0) {
        sc.input_fname = test_dir() / image_fname;
    } else {
        std::filesystem::path offset_name = sc.outdir / "offset_file";

        std::ifstream in(  test_dir() / image_fname, std::ios::binary);
        std::ofstream out( offset_name );
        in.seekg(offset);
        char ch;
        size_t written = 0;
        while (in.get(ch)) {
            out << ch;
            written ++;
        }
        in.close();
        out.close();
        sc.input_fname = offset_name;
    }

    feature_recorder_set::flags_t frs_flags;
    frs_flags.pedantic = true;          // for testing
    auto *xreport = new dfxml_writer(sc.outdir / "report.xml", false);
    scanner_set ss(sc, frs_flags, xreport);
    //ss.debug_flags.debug_scanners_same_thread = true; // run everything in the same thread
    ss.add_scanners(scanners_builtin);
    ss.apply_scanner_commands();

    if (image_fname != "" ) {
        try {
            auto p = image_process::open( sc.input_fname, false, 65536, 65536);
            std::stringstream strs;
            Phase1::Config cfg; // config for the image_processing system
            cfg.opt_quiet = true;       // do not need status reports
            Phase1 phase1(cfg, *p, ss, strs);
            phase1.dfxml_write_create( 0, nullptr);

            assert (ss.get_threading() == false);
            ss.phase_scan();
            phase1.phase1_run();
            delete p;
        } catch (image_process::NoSuchFile &e) {
            std::cerr << "sc.input_fname=" << sc.input_fname << " no such file: " << e.what() << std::endl;
            bool file_found=false;
            REQUIRE(file_found);
        }
    }
    ss.shutdown();

    /* There should be nothing in the work queue */
    assert( ss.sbufs_in_queue == 0);
    assert( ss.bytes_in_queue == 0);

    xreport->pop("dfxml");
    xreport->close();
    delete xreport;

    for (const auto &exp : expected ) {

        std::filesystem::path fname  = sc.outdir / exp.fname;
        bool found = false;
        for (int pass=0 ; pass<2 && !found;pass++){
            std::string line;
            std::ifstream inFile;
            inFile.open(fname);
            if (!inFile.is_open()) {
                throw std::runtime_error("validate_scanners:[phase1] Could not open "+fname.string());
            }
            while (std::getline(inFile, line) && !found) {
                switch (pass) {
                case 0:
                    if (feature_match(exp, line)){
                        found = true;
                    }
                    break;
                case 1:
                    std::cerr << fname << ":" << line << std::endl; // print the file the second time through
                    break;
                }

            }
        }
        if (!found){
            std::cerr << fname << " did not find"
                      << " pos=" << exp.feature.pos
                      << " feature=" << exp.feature.feature
                      << " context=" << exp.feature.context
                      << std::endl;
        }
        REQUIRE(found);
    }
    REQUIRE(start_sbuf_count == sbuf_t::sbuf_count);
    return sc.outdir;
}


bool validate_files(const std::filesystem::path &fn0, const std::filesystem::path &fn1)
{
    std::ifstream in0( fn0, std::ios::binary);
    std::ifstream in1( fn1, std::ios::binary);
    REQUIRE( in0.is_open());
    REQUIRE( in1.is_open());
    int errors = 0;
    for(size_t i=0;;i++) {
        uint8_t ch0,ch1;
        in0 >> ch0;
        in1 >> ch1;
        if (ch0 != ch1 ){
            if (errors==0) {
                std::cerr << "file 0 " << fn0 << std::endl;
                std::cerr << "file 1 " << fn1 << std::endl;
            }
            std::cerr << "i=" << i << "  ch0=" << static_cast<u_int>(ch0) << " ch1=" << static_cast<u_int>(ch1) << std::endl;
            errors += 1;
        }
        if (in0.eof() || in1.eof()) break;
    }
    return errors == 0;
}


TEST_CASE("test_aes", "[phase1]") {
    /* Test rotation with various sign extension snaffu */
    uint8_t in[4];
    in[0] = 0;
    in[1] = 0xf1;
    in[2] = 2;
    in[3] = 0xf3;
    rotate32x8(in);
    REQUIRE(in[0] == 0xf1);
    REQUIRE(in[1] == 2);
    REQUIRE(in[2] == 0xf3);
    REQUIRE(in[3] == 0);

    /* Test rotation with various sign extension snaffu */
    in[0] = 0xff;
    in[1] = 1;
    in[2] = 0xf2;
    in[3] = 3;
    rotate32x8(in);
    REQUIRE(in[0] == 1);
    REQUIRE(in[1] == 0xf2);
    REQUIRE(in[2] == 3);
    REQUIRE(in[3] == 0xff);

    /* Try with sign extension */

    std::vector<Check> ex3 {
        Check("aes_keys.txt", Feature("496", "a2 6e 0e 4c 06 c4 bb bf 5d 62 8b c7 f8 b3 91 b6", "AES128")),
        Check("aes_keys.txt", Feature("1120", "dc d2 05 18 c4 16 c0 e2 8e d8 59 9c 86 ed e8 e6", "AES128")),
        Check("aes_keys.txt", Feature("7008", "09 23 e0 4d 40 44 57 1f 55 bf 43 bc ac 06 11 04 45 63 03 a1 52 c5 4c 16 ba a6 96 e9 a6 18 80 65", "AES256")),
        Check("aes_keys.txt", Feature("7304", "00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14 15 16 17 18 19 1a 1b 1c 1d 1e 1f", "AES256"))
    };
    validate("ram_2pages.bin", ex3);
}


/* print the key schedules for several AES keys and then test them */
void validate_aes128_key(uint8_t key[16])
{
    const size_t AES128_KEY_SCHEDULE_SIZE = 176;
    uint8_t schedule[AES128_KEY_SCHEDULE_SIZE];
    create_aes128_schedule(key, schedule);
#if 0
    for(int i=0; i<AES128_KEY_SCHEDULE_SIZE;i++){
        printf("%02x ",schedule[i]);
    }
    printf("\n");
#endif
    printf("valid schedule: %d\n",valid_aes128_schedule(schedule));
    sbuf_t *keybuf = sbuf_t::sbuf_new(pos0_t(), schedule, sizeof(schedule), sizeof(schedule));
    printf("histogram count: %zu (out of %zu characters)\n",keybuf->get_distinct_character_count(),sizeof(schedule));
    delete keybuf;
}

TEST_CASE("schedule_aes", "[phase1]") {
    uint8_t key1[16] {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}; // all zeros is not a valid AES key
    validate_aes128_key(key1);
    uint8_t key2[16] {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}; // all zeros is not a valid AES key
    validate_aes128_key(key2);
    uint8_t key3[16] {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3}; // all zeros is not a valid AES key
    validate_aes128_key(key3);
}

TEST_CASE("test_base16json", "[phase1]") {
    std::vector<Check> ex2 {
        Check("json.txt",
              Feature( "50-BASE16-0",
                       "[{\"1\": \"one@base16_company.com\"}, "
                       "{\"2\": \"two@base16_company.com\"}, "
                       "{\"3\": \"two@base16_company.com\"}]",
                       "41e3ec783b9e2c2ffd93fe82079b3eef8579a6cd")),

        Check("email.txt",
              Feature( "50-BASE16-8",
                       "one@base16_company.com",
                       "[{\"1\": \"one@base16_company.com\"}, {\"2\": \"two@b")),

    };
    validate("test_base16json.txt", ex2);
}

TEST_CASE("test_ccn", "[phase1]") {
    auto *sbufp = map_file( "ccns.txt" );
    auto outdir = test_scanner( scan_accts, sbufp); // deletes sbufp
    auto ccns_txt = getLines( outdir / "ccn.txt" );
    REQUIRE( requireFeature(ccns_txt,"371449635398431"));
    REQUIRE( requireFeature(ccns_txt,"378282246310005"));
}

TEST_CASE("test_elf", "[phase1]") {
    std::vector<Check> ex {
        Check("elf.txt", Feature( "0", "9e218cee3b190e8f59ef323b27f4d339481516e9", "<ELF class=\"ELFCLASS64\" data=\"ELFDATA2LSB\" osabi=\"ELFOSABI_NONE\" abiversion=\"0\" >*"))
    };
    validate("hello_elf", ex);

}

TEST_CASE("test_gzip", "[phase1]") {
    std::vector<Check> ex3 {
        Check("email.txt", Feature( "0-GZIP-0", "hello@world.com", "hello@world.com\\012"))
    };
    validate("test_hello.gz", ex3);
}

TEST_CASE("test_json", "[phase1]") {
    std::vector<Check> ex1 {
        Check("json.txt", Feature( "0", JSON1, "ef2b5d7ee21e14eeebb5623784f73724218ee5dd")),
    };
    validate("test_json.txt", ex1);
}

TEST_CASE("KML_Samples.kml","[phase1]"){
    std::vector<Check> ex4 {
        Check("kml.txt",
              Feature( "0",
                       "kml/000/0.kml",
                       "<fileobject><filename>kml/000/0.kml</filename><filesize>35919</filesize><hashdigest type='sha1'>"
                       "cffc78e27ac32414b33d595a0fefcb971eaadaa3</hashdigest></fileobject>"))
    };
    validate("KML_Samples.kml", ex4);
}

TEST_CASE("test_jpeg_rar", "[phase1]") {
    std::vector<Check> ex2 {
        Check("jpeg_carved.txt",
              Feature( "13259-RAR-0", "jpeg_carved/000/13259-RAR-0.jpg"))

    };
    validate("jpegs.rar", ex2);
}

TEST_CASE("test_net1", "[phase1]") {
    std::vector<Check> ex2 {
        Check("ip.txt", Feature( "40", "192.168.0.91", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "40", "192.168.0.55", "struct ip R (dst) cksum-ok")),
        Check("ip_histogram.txt", Feature( "n=1", "192.168.0.91")),
        Check("ip_histogram.txt", Feature( "n=1", "192.168.0.55"))
    };
    auto outdir = validate("ntlm1.pcap", ex2, false);
    /* The output file should equal the input file */
    REQUIRE(validate_files(test_dir() / "ntlm1.pcap", outdir / "packets.pcap"));
}

TEST_CASE("test_net2", "[phase1]") {
    std::vector<Check> ex2 {
        Check("ip.txt", Feature( "40", "192.168.0.91", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "40", "192.168.0.55", "struct ip R (dst) cksum-ok")),
        Check("ip.txt", Feature( "482", "192.168.0.55", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "482", "192.168.0.91", "struct ip R (dst) cksum-ok")),
        Check("ip_histogram.txt", Feature( "n=2", "192.168.0.91")),
        Check("ip_histogram.txt", Feature( "n=2", "192.168.0.55"))
    };
    auto outdir = validate("ntlm2.pcap", ex2);
    REQUIRE(validate_files(test_dir() / "ntlm2.pcap", outdir / "packets.pcap"));
}

/* Look at a file with three packets */
TEST_CASE("test_net3", "[phase1]") {
    std::vector<Check> ex2 {
        Check("ip.txt", Feature( "40", "192.168.0.91", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "40", "192.168.0.55", "struct ip R (dst) cksum-ok")),
        Check("ip.txt", Feature( "482", "192.168.0.55", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "482", "192.168.0.91", "struct ip R (dst) cksum-ok")),
        Check("ip.txt", Feature( "1010", "192.168.0.91", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "1010", "192.168.0.55", "struct ip R (dst) cksum-ok")),
        Check("ip_histogram.txt", Feature( "n=3", "192.168.0.91")),
        Check("ip_histogram.txt", Feature( "n=3", "192.168.0.55"))
    };
    validate("ntlm3.pcap", ex2);
}

/* Look at a file with three packets with an offset of 10, to see if we can find the packets even when the PCAP file header is missing */
TEST_CASE("test_net3+10", "[phase1]") {
    std::vector<Check> ex2 {
        Check("ip.txt", Feature( "30", "192.168.0.91", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "30", "192.168.0.55", "struct ip R (dst) cksum-ok")),
        Check("ip.txt", Feature( "472", "192.168.0.55", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "472", "192.168.0.91", "struct ip R (dst) cksum-ok")),
        Check("ip.txt", Feature( "1000", "192.168.0.91", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "1000", "192.168.0.55", "struct ip R (dst) cksum-ok")),
        Check("ip_histogram.txt", Feature( "n=3", "192.168.0.91")),
        Check("ip_histogram.txt", Feature( "n=3", "192.168.0.55"))
    };
    validate("ntlm3.pcap", ex2, false, 10);
}

/* Look at a file with three packets with an offset of 24, to see if we can find the packets even when the PCAP record header is missing.
 * (of course, it's only missing for one.)
 */
TEST_CASE("test_net3+24", "[phase1]") {
    std::vector<Check> ex2 {
        Check("ip.txt", Feature( "16", "192.168.0.91", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "16", "192.168.0.55", "struct ip R (dst) cksum-ok")),
        Check("ip.txt", Feature( "458", "192.168.0.55", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "458", "192.168.0.91", "struct ip R (dst) cksum-ok")),
        Check("ip.txt", Feature( "986", "192.168.0.91", "struct ip L (src) cksum-ok")),
        Check("ip.txt", Feature( "986", "192.168.0.55", "struct ip R (dst) cksum-ok")),
        Check("ip_histogram.txt", Feature( "n=3", "192.168.0.91")),
        Check("ip_histogram.txt", Feature( "n=3", "192.168.0.55"))
    };
    validate("ntlm3.pcap", ex2, false, 24);
}

TEST_CASE("test_net80", "[phase1]") {
    std::vector<Check> ex2 {
        Check("ip.txt", Feature( "40", "192.168.0.91", "struct ip L (src) cksum-ok")),
        Check("ip_histogram.txt", Feature( "n=80", "192.168.0.91"))
    };
    validate("ntlm80.pcap", ex2);
}

TEST_CASE("test_winpe", "[phase1]") {
    std::vector<Check> ex2 {
        Check("winpe.txt", Feature( "0",
                                    "074b9b371de190a96fb0cb987326cd238142e9d1",
                                    "<PE><FileHeader Machine=\"IMAGE_FILE_MACHINE_I386*"))
    };
    validate("hello_win64_exe", ex2);
}

/****************************************************************
 * Test process_dir
 */
TEST_CASE("process_dir", "[process_dir]") {

    /* This should throw NoSuchFile because there is is an E01 file */
    REQUIRE_THROWS_AS(image_process::open( test_dir(), true, 65536, 65536), image_process::FoundDiskImage);

    /* Get the right return code */
    std::filesystem::path inpath = test_dir();
    std::string inpath_string = inpath.string();
    std::filesystem::path outdir = NamedTemporaryDirectory();
    std::string outdir_string = outdir.string();
    std::stringstream ss;
    const char *argv[] = {"bulk_extractor", notify(), "-Ro", outdir_string.c_str(), inpath_string.c_str(), nullptr};
    int ret = run_be(ss, ss, argv);
    REQUIRE( ret==6 );

    /* This should return the jpegs */
    image_process *p = nullptr;
    try {
        p = image_process::open( test_dir() / "jpegs", true, 65536, 65536);
    }
    catch (image_process::FoundDiskImage &e) {
        std::cerr << "FoundDiskImage: " << e.what() << std::endl;
        exit(1);
    }
    catch (image_process::IsADirectory &e) {
        std::cerr << "IsAdirectory: " << e.what() << std::endl;
        exit(1);
    }
    catch (image_process::NoSuchFile &e) {
        std::cerr << "NoSuchFile: " << e.what() << std::endl;
        std::cerr << "Current Directory: " << std::filesystem::current_path() << std::endl;
        exit(1);
    }

    int count = 0;
    for( image_process::iterator it = p->begin(); it != p->end(); ++it ){
        count++;
        pos0_t pos0 = it.get_pos0();
        REQUIRE( pos0.str().find(".jpg") != std::string::npos );
    }
    delete p;
}


/****************************************************************
 * Test restarter
 ** test sbufs (which is this here?
 */

sbuf_t *make_sbuf()
{
    auto sbuf = new sbuf_t("Hello World!");
    return sbuf;
}

/* Test that sbuf data  are not copied when moved to a child.*/
std::atomic<int> counter{0};
const uint8_t *sbuf_buf_loc = nullptr;
void test_process_sbuf(sbuf_t *sbuf)
{
    if (sbuf_buf_loc != nullptr) {
        REQUIRE( sbuf_buf_loc == sbuf->get_buf() );
    }
    delete sbuf;
}

TEST_CASE("sbuf_no_copy", "[threads]") {
    for(int i=0;i<100;i++){
        auto sbuf = make_sbuf();
        sbuf_buf_loc = sbuf->get_buf();
        test_process_sbuf(sbuf);
    }
}

/****************************************************************/
TEST_CASE("image_process", "[phase1]") {
    image_process *p = nullptr;
    REQUIRE_THROWS_AS( p = image_process::open( "no-such-file", false, 65536, 65536), image_process::NoSuchFile);
    REQUIRE_THROWS_AS( p = image_process::open( "no-such-file", false, 65536, 65536), image_process::NoSuchFile);
    p = image_process::open( test_dir() / "test_json.txt", false, 65536, 65536);
    REQUIRE( p != nullptr );
    int times = 0;

    for(auto it = p->begin(); it!=p->end(); ++it){
        REQUIRE( times==0 );
        sbuf_t *sbufp = it.sbuf_alloc();

        REQUIRE( sbufp->bufsize == 79 );
        REQUIRE( sbufp->pagesize == 79 );
        delete sbufp;
        times += 1;
    }
    REQUIRE(times==1);
    delete p;
}

/****************************************************************
 ** Test the path printer
 **/
TEST_CASE("path-printer1", "[path_printer]") {
    scanner_config sc;
    sc.input_fname = test_dir() / "test_hello.512b.gz";
    sc.enable_all_scanners();
    sc.allow_recurse = true;

    scanner_set ss(sc, feature_recorder_set::flags_disabled(), nullptr);
    ss.add_scanners(scanners_builtin);
    ss.apply_scanner_commands();

    image_process *reader = image_process::open( sc.input_fname, false, 65536, 65536 );
    std::stringstream str;
    class path_printer pp(ss, reader, str);
    pp.process_path("512-GZIP-0/h");    // create a hex dump

    REQUIRE(str.str() == "0000: 6865 6c6c 6f40 776f 726c 642e 636f 6d0a hello@world.com.\n");
    str.str("");

    pp.process_path("512-GZIP-2/r");    // create a hex dump with a different path and the /r
    REQUIRE( str.str() == "14\r\nllo@world.com\n" );
    delete reader;
}
