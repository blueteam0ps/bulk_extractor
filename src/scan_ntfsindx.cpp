/**
 * Plugin: scan_ntfsindx
 * Purpose: Find all $INDEX_ALLOCATION INDX record into one file
 * Reference: http://www.digital-evidence.org/fsfa/
 * Teru Yamazaki(@4n6ist) - https://github.com/4n6ist/bulk_extractor-rec
 **/

#include <iostream>
#include <fstream>
#include <string>
#include <stdlib.h>
#include <strings.h>
//#include <cerrno>
#include <sstream>
#include <vector>

#include "config.h"

#include "be13_api/scanner_params.h"

#include "utf8.h"

#define SECTOR_SIZE 512
#define CLUSTER_SIZE 4096
#define FEATURE_FILE_NAME "ntfsindx_carved"


// check $INDEX_ALLOCATION INDX Signature
// return: 1 - valid INDX record, 2 - corrupt INDX record, 0 - not INDX record
int8_t check_indxrecord_signature(size_t offset, const sbuf_t &sbuf) {
    int16_t fixup_offset;
    int16_t fixup_count;
    int16_t fixup_value;
    int16_t i;

    // start with "INDX"
    if (sbuf[offset] == 0x49 && sbuf[offset + 1] == 0x4E &&
        sbuf[offset + 2] == 0x44  && sbuf[offset + 3] == 0x58) {

        fixup_offset = sbuf.get16i(offset + 4);
        if (fixup_offset <= 0 || fixup_offset >= SECTOR_SIZE)
            return 0;
        fixup_count = sbuf.get16i(offset + 6);
        if (fixup_count <= 0 || fixup_count >= SECTOR_SIZE)
            return 0;

        fixup_value = sbuf.get16i(offset + fixup_offset);

        for(i=1;i<fixup_count;i++){
            if (fixup_value != sbuf.get16i(offset + (SECTOR_SIZE * i) - 2))
                return 2;
        }
        return 1;
    } else {
        return 0;
    }
}

// determine type of INDX
// return: 1 - FILENAME INDX record, 2 - ObjId-O INDX record, 0 - Other INDX record (Secure-SDH, Secure-SII, etc.)
int8_t check_indxrecord_type(size_t offset, const sbuf_t &sbuf) {

    // 4 FILETIME pattern
    if (sbuf[offset + 95] == 0x01 && sbuf[offset + 103] == 0x01 &&
        sbuf[offset + 111] == 0x01 && sbuf[offset + 119] == 0x01)
        return 1;
    // ObjId-O magic number
    else if (sbuf[offset + 64] == 0x20 && sbuf[offset + 72] == 0x58)
        return 2;
    else
        return 0;
}

extern "C"

void scan_ntfsindx(scanner_params &sp)
{
    sp.check_version();
    if(sp.phase==scanner_params::PHASE_INIT){
        sp.info->set_name("ntfsindx");
        sp.info->author          = "Teru Yamazaki";
        sp.info->description     = "Scans for NTFS $INDEX_ALLOCATION INDX record";
        sp.info->scanner_version = "1.1";
        sp.info->scanner_flags.scanner_wants_filesystems = true;
        struct feature_recorder_def::flags_t carve_flag;
        carve_flag.carve = true;
        sp.info->feature_defs.push_back( feature_recorder_def(FEATURE_FILE_NAME, carve_flag));
        return;
    }
    if(sp.phase==scanner_params::PHASE_SCAN){
        const sbuf_t &sbuf = *(sp.sbuf);
        feature_recorder &ntfsindx_recorder = sp.named_feature_recorder(FEATURE_FILE_NAME);

        // search for NTFS $INDEX_ALLOCATION INDX record in the sbuf
        size_t offset = 0;
        size_t stop = sbuf.pagesize;
        size_t total_record_size=0;
        int8_t result_type, record_type;

        while (offset < stop) {

            result_type = check_indxrecord_signature(offset, sbuf);
            total_record_size = CLUSTER_SIZE;

            if (result_type == 1) {

                record_type = check_indxrecord_type(offset, sbuf);
                if(record_type == 1) {

                    // found one valid INDX record then also checks following valid records and writes all at once
                    while (true) {
                        if (offset+total_record_size >= stop)
                            break;

                        result_type = check_indxrecord_signature(offset+total_record_size, sbuf);

                        if (result_type == 1) {
                            record_type = check_indxrecord_type(offset+total_record_size, sbuf);
                            if (record_type == 1)
                                total_record_size += CLUSTER_SIZE;
                            else
                                break;
                        }
                        else
                            break;
                    }
                    ntfsindx_recorder.carve(sbuf_t(sbuf,offset,total_record_size), ".INDX");
                }
                else if(record_type == 2) {
                    ntfsindx_recorder.carve(sbuf_t(sbuf,offset,total_record_size),".INDX_ObjId-O");
                }
                else { // 0 - Other INDX record (Secure-SDH, Secure-SII, etc.)
                    ntfsindx_recorder.carve(sbuf_t(sbuf,offset,total_record_size),".INDX_Misc");
                }
            }
            else if (result_type == 2) {
                ntfsindx_recorder.carve(sbuf_t(sbuf,offset,total_record_size),".INDX_corrupted");
            }
            else { // result_type == 0
            }
            offset += total_record_size;
        }
    }
}
