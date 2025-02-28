/**
 * scan_exif: - custom exif scanner for identifying specific features for bulk_extractor.
 * Also includes JPEG carver
 *
 * Revision history:
 * 2013-jul    slg - added JPEG carving.
 * 2011-dec-12 bda - Ported from file scan_exif.cpp.
 */

#include "config.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <cassert>
#include <algorithm>


#include "scan_exif.h"
#include "be13_api/scanner_params.h"
#include "be13_api/utils.h"
#include "be13_api/formatter.h"

#include "dfxml_cpp/src/dfxml_writer.h"

#include "exif_reader.h"
#include "unicode_escape.h"

// these are tunable
static size_t min_jpeg_size = 1000; // don't carve smaller than this

/****************************************************************
 *** formatting code
 ****************************************************************/

/**
 * Used for helping to convert TIFF's GPS format to decimal lat/long
 */
bool exif_debug = false;

static double be_stod( std::string s )
{
    double d=0;
    sscanf( s.c_str(),"%lf",&d );
    return d;
}

static double rational( std::string s )
{
    std::vector<std::string> parts = split( s,'/' );
    if ( parts.size()!=2 ) return be_stod( s );	// no slash, so return without
    double top = be_stod( parts[0] );
    double bot = be_stod( parts[1] );
    return bot>0 ? top / bot : top;
}

static std::string fix_gps( std::string s )
{
    std::vector<std::string> parts = split( s,' ' );
    if ( parts.size()!=3 ) return s;	// return the original
    double res = rational( parts[0] ) + rational( parts[1] )/60.0 + rational( parts[2] )/3600.0;
    return std::to_string( res );
}

static std::string fix_gps_ref( std::string s )
{
    if ( s=="W" || s=="S" ) return "-";
    return "";
}


/************************************************************
 * exif_reader
 *************************************************************
 */
namespace psd_reader {
    static uint32_t get_uint8_psd( const sbuf_t &exif_sbuf, uint32_t offset );
    static uint32_t get_uint16_psd( const sbuf_t &exif_sbuf, uint32_t offset );
    static uint32_t get_uint32_psd( const sbuf_t &exif_sbuf, uint32_t offset );
    static bool psd_debug = false;

    /**
     * finds the TIFF header within the PSD region, or 0 if invalid
     */
    size_t get_tiff_offset_from_psd( const sbuf_t &exif_sbuf ) {
        // validate that the PSD header is "8BPS" ver. 01
        // Ref. Photoshop header, e.g. http://www.fileformat.info/format/psd/egff.htm
        // or exiv2-0.22/src/psdimage.cpp
        if ( exif_sbuf.pagesize < 26
         || exif_sbuf[0]!='8'
         || exif_sbuf[1]!='B'
         || exif_sbuf[2]!='P'
         || exif_sbuf[3]!='S'
         || exif_sbuf[4]!= 0
         || exif_sbuf[5]!= 1 ) {
            if ( psd_debug ) std::cerr << "scan_exif.get_tiff_offset_from_psd header rejected\n";
            return 0;
        }

        // validate that the 6 reserved bytes are 0
        if ( exif_sbuf[6]!=0 || exif_sbuf[7]!=0 || exif_sbuf[8]!=0 || exif_sbuf[9]!=0
         || exif_sbuf[10]!=0 || exif_sbuf[11]!=0 ) {
            if ( psd_debug ) std::cerr << "scan_exif.get_tiff_offset_from_psd reserved bytes rejected\n";
            return 0;
        }

        // get the size of the color mode data section that is skipped over
        uint32_t color_mode_data_section_length = get_uint32_psd( exif_sbuf, 26 );

        // get the size of the list of resource blocks
        uint32_t resource_length = get_uint32_psd( exif_sbuf, 26 + 4 + color_mode_data_section_length );

        // define the offset to the list of resource blocks
        size_t resource_offset_start = 26 + 4 + color_mode_data_section_length + 4;

        // identify the offset to the end of the list of resource blocks
        size_t resource_offset_end = resource_offset_start + resource_length;

        // loop through resource blocks looking for PhotoShop 7.0 Resource ID ExifInfo, 0x0422
        size_t resource_offset = resource_offset_start;
        while ( resource_offset < resource_offset_end ) {
	      //uint32_t resource_type = get_uint32_psd( exif_sbuf, resource_offset + 0 );
            uint16_t resource_id = get_uint16_psd( exif_sbuf, resource_offset + 4 );
            uint8_t resource_name_length = get_uint8_psd( exif_sbuf, resource_offset + 6 ) & 0xfe;
            uint32_t resource_size = get_uint32_psd( exif_sbuf, resource_offset + 8 + resource_name_length );

            // align resource size to word boundary
            resource_size = ( resource_size + 1 ) & 0xfffe;

            // check to see if this resource is ExifInfo
            if ( resource_id == 0x0422 ) {
                size_t tiff_start = resource_offset + 8 + resource_name_length + 4;
                if ( psd_debug ) std::cerr << "scan_exif.get_tiff_offset_from_psd accepted at tiff_start " << tiff_start << "\n";
                return tiff_start;
            }
            resource_offset += 8 + resource_name_length + 4 + resource_size;
        }
        if ( psd_debug ) std::cerr << "scan_exif.get_tiff_offset_from_psd ExifInfo resource was not found\n";
        return 0;
    }

    static uint32_t get_uint8_psd( const sbuf_t &exif_sbuf, uint32_t offset ) {
        // check for EOF
        if ( offset + 1 > exif_sbuf.bufsize ) return 0;

        // return uint32 in big-endian Motorola byte order
        return static_cast<uint32_t>( exif_sbuf[offset + 1] ) << 0;
    }

    static uint32_t get_uint16_psd( const sbuf_t &exif_sbuf, uint32_t offset ) {
        // check for EOF
        if ( offset + 2 > exif_sbuf.bufsize ) return 0;

        // return uint32 in big-endian Motorola byte order
        return static_cast<uint32_t>( exif_sbuf[offset + 0] ) << 8
             | static_cast<uint32_t>( exif_sbuf[offset + 1] ) << 0;
    }

    static uint32_t get_uint32_psd( const sbuf_t &exif_sbuf, uint32_t offset ) {
        // check for EOF
        if ( offset + 4 > exif_sbuf.bufsize ) return 0;

        // return uint32 in big-endian Motorola byte order
        return static_cast<uint32_t>( exif_sbuf[offset + 0] ) << 24
             | static_cast<uint32_t>( exif_sbuf[offset + 1] ) << 16
             | static_cast<uint32_t>( exif_sbuf[offset + 2] ) << 8
             | static_cast<uint32_t>( exif_sbuf[offset + 3] ) << 0;
    }
}



/**
 * record exif data in well-formatted XML.
 */
void exif_scanner::record_exif_data( const pos0_t &pos0, std::string hash_hex )
{
    // do not record the exif feature if there are no entries
    if ( entries.size() == 0 ) {
        return;
    }

    // compose xml from all entries
    if ( exif_debug ) std::cerr << pos0 << " scan_exif recording data for entry" << std::endl;

    std::stringstream sts;
    sts << "<exif>";
    for ( const auto &it: entries ) {

        // prepare by escaping XML codes.
        if ( exif_debug ) std::cerr << pos0 << " scan_exif fed before xmlescape: "
                                  << it->name << ":" << it->value << std::endl;
        std::string prepared_value = dfxml_writer::xmlescape( it->value );
        if ( exif_debug )  std::cerr << pos0 << " scan_exif fed after xmlescape: " << prepared_value << std::endl;

        // do not report entries that have empty values
        if ( prepared_value.length() == 0 ) {
            continue;
        }

        // validate against maximum entry size
        if ( exif_debug ) {
            if ( prepared_value.size() > jpeg_validator::MAX_ENTRY_SIZE ) {
                throw std::runtime_error( Formatter()
                                    << "exif_scanner::record_exif_data prepared_value.size()=="
                                    << prepared_value.size() );
            }
        }


        if ( exif_debug )  std::cerr << pos0 << "  point3" << std::endl;
        sts << "<" << it->get_full_name() << ">" << prepared_value << "</" << it->get_full_name() << ">";
        if ( exif_debug )  std::cerr << pos0 << "  point4" << std::endl;
    }
    sts << "</exif>";

    // record the formatted exif entries
    exif_recorder.write( pos0, hash_hex, sts.str() );
}

/**
 * Record GPS data as comma separated values.
 * Note that GPS data is considered to be present when a GPS IFD entry is present
 * that is not just a time or date entry.
 */
void exif_scanner::record_gps_data( const pos0_t &pos0, std::string hash_hex )
{
    // desired GPS strings
    std::string gps_time, gps_date, gps_lon_ref, gps_lon, gps_lat_ref;
    std::string gps_lat, gps_ele, gps_speed, gps_course;

    // date if GPS date is not available
    std::string exif_time, exif_date;

    bool has_gps = false;
    bool has_gps_date = false;

    // get the desired GPS strings from the entries
    for ( const auto &it: entries ) {

        // get timestamp from EXIF IFD just in case it is not available from GPS IFD
        if ( it->name.compare( "DateTimeOriginal" ) == 0 ) {
            exif_time = it->value;

            if ( exif_debug ) std::cerr << "scan_exif.format_gps_data exif_time: " << exif_time << "\n";

            if ( exif_time.length() == 19 ) {
                // reformat timestamp to standard ISO8601

                /* change "2011/06/25 12:20:11" to "2011-06-25 12:20:11" */
                /*             ^  ^                     ^  ^             */
                if ( exif_time[4] == '/' ) {
                    exif_time[4] = '-';
                }
                if ( exif_time[7] == '/' ) {
                    exif_time[7] = '-';
                }

                /* change "2011:06:25 12:20:11" to "2011-06-25 12:20:11" */
                /*             ^  ^                     ^  ^             */
                if ( exif_time[4] == ':' ) {
                    exif_time[4] = '-';
                }
                if ( exif_time[7] == ':' ) {
                    exif_time[7] = '-';
                }

                /* Change "2011-06-25 12:20:11" to "2011-06-25T12:20:11" */
                /*                   ^                        ^          */
                if ( exif_time[10] == ' ' ) {
                    exif_time[10] = 'T';
                }
            }
        }

        if ( it->ifd_type == IFD0_GPS ) {

            // get GPS values from IFD0's GPS IFD
            if ( it->name.compare( "GPSTimeStamp" ) == 0 ) {
                has_gps_date = true;
                gps_time = it->value;
                // reformat timestamp to standard ISO8601
                // change "12 20 11" to "12:20:11"
                if ( gps_time.length() == 8 ) {
                    if ( gps_time[4] == ' ' ) {
                        gps_time[4] = ':';
                    }
                    if ( gps_time[7] == ' ' ) {
                        gps_time[7] = ':';
                    }
                }
            } else if ( it->name.compare( "GPSDateStamp" ) == 0 ) {
                has_gps_date = true;
                gps_date = it->value;
                // reformat timestamp to standard ISO8601
                // change "2011:06:25" to "2011-06-25"
                if ( gps_date.length() == 10 ) {
                    if ( gps_date[4] == ':' ) {
                        gps_date[4] = '-';
                    }
                    if ( gps_date[7] == ':' ) {
                        gps_date[7] = '-';
                    }
                }
            } else if ( it->name.compare( "GPSLongitudeRef" ) == 0 ) {
                has_gps = true;
                gps_lon_ref = fix_gps_ref( it->value );
            } else if ( it->name.compare( "GPSLongitude" ) == 0 ) {
                has_gps = true;
                gps_lon = fix_gps( it->value );
            } else if ( it->name.compare( "GPSLatitudeRef" ) == 0 ) {
                has_gps = true;
                gps_lat_ref = fix_gps_ref( it->value );
            } else if ( it->name.compare( "GPSLatitude" ) == 0 ) {
                has_gps = true;
                gps_lat = fix_gps( it->value );
            } else if ( it->name.compare( "GPSAltitude" ) == 0 ) {
                has_gps = true;
                gps_ele = std::to_string( rational( it->value ) );
            } else if ( it->name.compare( "GPSSpeed" ) == 0 ) {
                has_gps = true;
                gps_speed = std::to_string( rational( it->value ) );
            } else if ( it->name.compare( "GPSTrack" ) == 0 ) {
                has_gps = true;
                gps_course = it->value;
            }
        }
    }

    // compose the formatted gps value from the desired GPS strings
    // NOTE: desired date format is "2011-06-25T12:20:11" made from "2011:06:25" and "12 20 11"
    if ( has_gps ) {
        // report GPS
        std::stringstream sts;
        if ( has_gps_date ) {
            // use GPS data with GPS date
            sts << gps_date << "T" << gps_time << ",";
        } else {
            // use GPS data with date from EXIF
            sts << exif_time << ",";
        }
        sts << gps_lat_ref << gps_lat << "," << gps_lon_ref << gps_lon << ",";
        sts << gps_ele << "," << gps_speed << "," << gps_course;

        // record the formatted GPS entries
        gps_recorder.write( pos0, hash_hex, sts.str() );

    } else {
        // no GPS to report
    }
}

size_t exif_scanner::process_possible_jpeg( const sbuf_t &sbuf, bool found_start )
{
    // get hash for this exif
    size_t ret = 0;
    std::string hex_hash {"00000000000000000000000000000000"};
    if ( found_start ){
        jpeg_validator::results_t res = jpeg_validator::validate_jpeg( sbuf );
        if ( exif_scanner_debug ) std::cerr << "res.len=" << res.len << " res.how=" << ( int )( res.how ) << "\n";

        // Is it valid?
        if ( res.len <= 0 ) return 0;

        // Should we carve?
        if ( res.how==jpeg_validator::COMPLETE || res.len > static_cast<ssize_t>( min_jpeg_size ) ) {
            if ( exif_scanner_debug ) fprintf( stderr,"CARVING1\n" );
            jpeg_recorder.carve( sbuf.slice( 0, res.len ), ".jpg", 0 );
            ret = res.len;
        }

        // Record the hash of the first 4K
        hex_hash = ss->hash( sbuf_t( sbuf,0,4096 ) );
    }

    /* Record entries ( if present ) in the feature files */
    record_exif_data( sbuf.pos0, hex_hash );
    record_gps_data( sbuf.pos0, hex_hash );
    entries.clear();
    return ret;
}

// search through sbuf for potential exif content
// When data is found, carve it depending on the carving mode, and then
// keep going.
void exif_scanner::scan( const sbuf_t &sbuf )
{
    // require at least this many bytes

    // If the margin is smaller than jpeg_validator::MIN_JPEG_SIZE,
    // end before the margin. Note that the sbuf is guarenteed to be larger than jpeg_validator::MIN_JPEG_SIZE.
    size_t limit = sbuf.pagesize;
    if ( sbuf.bufsize - sbuf.pagesize < jpeg_validator::MIN_JPEG_SIZE ) {
        assert ( sbuf.bufsize >= jpeg_validator::MIN_JPEG_SIZE );
        limit = sbuf.bufsize - jpeg_validator::MIN_JPEG_SIZE;
    }

    for ( size_t start=0; start < limit; start++ ) {
        // check for start of a JPEG.
        if ( sbuf[start + 0] == 0xff && sbuf[start + 1] == 0xd8 &&
            sbuf[start + 2] == 0xff && ( sbuf[start + 3] & 0xf0 ) == 0xe0 ) {

            // Does this JPEG have an EXIF?
            size_t possible_tiff_offset_from_exif = exif_reader::get_tiff_offset_from_exif ( sbuf.slice( start ) );
            if ( exif_scanner_debug ){
                std::cerr << "scan_exif.possible_tiff_offset_from_exif " << possible_tiff_offset_from_exif << "\n";
            }
            if ( ( possible_tiff_offset_from_exif != 0 )
                && tiff_reader::is_maybe_valid_tiff( sbuf.slice( start + possible_tiff_offset_from_exif ) ) ) {

                // TIFF in Exif is valid, so process TIFF
                size_t tiff_offset = start + possible_tiff_offset_from_exif;

                if ( exif_scanner_debug ){
                    std::cerr << "scan_exif Start processing validated Exif ffd8ff at start " << start << "\n";
                }

                // get entries for this exif
                try {
                    tiff_reader::read_tiff_data( sbuf.slice( tiff_offset ), entries );
                } catch ( exif_failure_exception_t &e ) {
                    // accept whatever entries were gleaned before the exif failure
                }
            }
            // Try to process if it is exif or not

            size_t skip_bytes = process_possible_jpeg( sbuf.slice( start ), true );
            if ( skip_bytes>1 ) start += skip_bytes - 1; // ( because the for loop will add 1 )
            if ( exif_scanner_debug ){
                std::cerr << "scan_exif Done processing JPEG/Exif ffd8ff at " << start << " len=" << skip_bytes << "\n";
            }
            continue;
        }
        // check for possible TIFF in photoshop PSD header
        if ( sbuf[start + 0] == '8' && sbuf[start + 1] == 'B' && sbuf[start + 2] == 'P' &&
             sbuf[start + 3] == 'S' && sbuf[start + 4] ==   0 && sbuf[start + 5] == 1 ) {
            if ( exif_scanner_debug ) std::cerr << "scan_exif checking 8BPS at start " << start << "\n";
            // perform thorough check for TIFF in photoshop PSD
            size_t possible_tiff_offset_from_psd = psd_reader::get_tiff_offset_from_psd( sbuf.slice( start ) );
            if ( exif_scanner_debug ){
                std::cerr << "scan_exif.psd possible_tiff_offset_from_psd " << possible_tiff_offset_from_psd << "\n";
            }
            if ( ( possible_tiff_offset_from_psd != 0 )
                && tiff_reader::is_maybe_valid_tiff( sbuf.slice( start + possible_tiff_offset_from_psd ) ) ) {
                // TIFF in PSD is valid, so process TIFF
                size_t tiff_offset = start + possible_tiff_offset_from_psd;

                // get entries for this exif
                try {
                    tiff_reader::read_tiff_data( sbuf.slice( tiff_offset ), entries );
                } catch ( exif_failure_exception_t &e ) {
                    // accept whatever entries were gleaned before the exif failure
                }

                if ( exif_scanner_debug ) {
                    std::cerr << "scan_exif Start processing validated Photoshop 8BPS at start " << start << " tiff_offset " << tiff_offset << "\n";
                }
                size_t skip = process_possible_jpeg( sbuf.slice( start ), true );
                if ( skip>1 ) start += skip-1;
                if ( exif_scanner_debug ){
                    std::cerr << "scan_exif Done processing validated Photoshop 8BPS at start " << start << "\n";
                }
            }
            continue;
        }
        // check for probable TIFF not in embedded header found above
        if ( ( sbuf[start + 0] == 'I' && sbuf[start + 1] == 'I' &&
             sbuf[start + 2] == 42  && sbuf[start + 3] == 0 ) // intel
            ||
            ( sbuf[start + 0] == 'M' && sbuf[start + 1] == 'M' &&
             sbuf[start + 2] == 0   && sbuf[start + 3] == 42 ) // Motorola
            ){

            // probably a match so check further
            if ( tiff_reader::is_maybe_valid_tiff( sbuf.slice( start ) ) ) {
                // treat this as a valid match
                //if ( debug ) std::cerr << "scan_exif Start processing validated TIFF II42 or MM42 at start "
                //<< start << ", last tiff_offset: " << tiff_offset << "\n";
                // get entries for this exif
                try {
                    tiff_reader::read_tiff_data( sbuf.slice( start ), entries );
                } catch ( exif_failure_exception_t &e ) {
                    // accept whatever entries were gleaned before the exif failure
                }

                // there is no MD5 because there is no associated file for this TIFF marker
                process_possible_jpeg( sbuf.slice( start ), false );
                if ( exif_scanner_debug ){
                    std::cerr << "scan_exif Done processing validated TIFF II42 or MM42 at start "
                              << start << "\n";
                }
            }
        }
    }
}

extern "C"
void scan_exif ( scanner_params &sp )
{
    sp.check_version();
    if ( sp.phase==scanner_params::PHASE_INIT ){
        sp.info->set_name( "exif" );
	sp.info->author          = "Bruce Allen";
	sp.info->scanner_version = "1.1";
        sp.info->description     = "Search for EXIF sections in JPEG files";
        sp.info->min_sbuf_size   = jpeg_validator::MIN_JPEG_SIZE;
        struct feature_recorder_def::flags_t xml_flag;
        xml_flag.xml = true;
        struct feature_recorder_def::flags_t carve_flag;
        carve_flag.carve = true;
	sp.info->feature_defs.push_back( feature_recorder_def( "exif", xml_flag ) );
	sp.info->feature_defs.push_back( feature_recorder_def( "gps" ) );
	sp.info->feature_defs.push_back( feature_recorder_def( "jpeg_carved", carve_flag ) );
        sp.get_scanner_config( "exif_debug",&exif_debug,"debug exif decoder" );
	return;
    }
    if ( sp.phase==scanner_params::PHASE_INIT2 ) {
    }
    if ( sp.phase==scanner_params::PHASE_SCAN ){
        /* Note: this is expensive ( creating and deleting the exif scanner each time ) */
        exif_scanner *escan = new exif_scanner( sp );
        escan->scan( *sp.sbuf );
        delete escan;
    }
}
