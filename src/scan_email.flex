%{
/* scan_email.flex
 *
 * Note below:
 * U_TLD1 is a regular expression that catches top level domains in UTF-16.
 * Also scans for ethernet addresses; addresses are validated by algorithm below.
 *
 * If you want better precision, use scan_email_lg
 */

#include <cstdlib>
#include <cstring>
#include <cctype>

#include "config.h"
#include "sbuf_flex_scanner.h"
#include "be13_api/utils.h"
#include "scan_email.h"

class email_scanner : public sbuf_scanner {
public:
      email_scanner(const scanner_params &sp):
          sbuf_scanner(*sp.sbuf),
          email_recorder(sp.named_feature_recorder("email")),
          rfc822_recorder(sp.named_feature_recorder("rfc822")),
          domain_recorder(sp.named_feature_recorder("domain")),
          url_recorder(sp.named_feature_recorder("url")),
          ether_recorder(sp.named_feature_recorder("ether")){
      }
      class feature_recorder &email_recorder;
      class feature_recorder &rfc822_recorder;
      class feature_recorder &domain_recorder;
      class feature_recorder &url_recorder;
      class feature_recorder &ether_recorder;

      /* Hard-coded program to reject wacky ethers that showed up a lot */
      bool valid_ether_addr(size_t pos){
	if (sbuf.memcmp((const uint8_t *)"00:00:00:00:00:00",pos,17)==0) return false;
	if (sbuf.memcmp((const uint8_t *)"00:11:22:33:44:55",pos,17)==0) return false;
	/* Perform a quick histogram analysis.
	 * For each group of characters, create a value based on the two digits.
	 * There is no need to convert them to their 'actual' value.
	 * Don't accept a histogram that has 3 values. That could be 11:11:11:11:22:33
	 * Require 4, 5 or 6.
	 * If we have 4 or more distinct values, then treat it good.
	 * Otherwise its is some pattern we don't want.
	 */
	std::set<uint16_t> ctr;
	for (uint32_t i=0;i<6;i++){	/* loop for each group of numbers */
            uint8_t ch1 = (sbuf)[pos+i*3];
            uint8_t ch2 = (sbuf)[pos+i*3+1];
            uint16_t val = (ch1<<8) + (ch2); /* create a value of the two characters (it's not */
            ctr.insert(val);
	}
	if (ctr.size()<4) return false;
        return true;		/* all tests pass */
      }

};
#define YY_EXTRA_TYPE email_scanner *                     /* holds our class pointer */
YY_EXTRA_TYPE yyemail_get_extra (yyscan_t yyscanner );    /* redundent declaration */
inline class email_scanner *get_extra(yyscan_t yyscanner) {return yyemail_get_extra(yyscanner);}

/* Address some common false positives in email scanner.
 * We don't need to do a full regular expression check, because one has already been done.
 */
bool extra_validate_email(const char *email)
{
    if (strstr(email,"..")) return false;
    return true;
}


/** return the position of the domain email address if one is present.
 */
ssize_t find_host_in_email(const sbuf_t &sbuf)
{
    ssize_t loc = sbuf.find('@');
    if (loc >= 0 && sbuf.bufsize > loc+1) {
        return loc+1;
    }
    return -1;
}

/*
 * Return the domain in a URL.
 */
// https://stackoverflow.com/questions/2616011/easy-way-to-parse-a-url-in-c-cross-platform
ssize_t find_host_in_url(const sbuf_t &sbuf, size_t *domain_len)
{
    std::string uri = sbuf.asString();
    size_t start =  uri.find("://", 0);
    if (start==std::string::npos) return -1;
    start += 3;
    size_t end = uri.find("/", start);
    if (end==std::string::npos) {
       *domain_len = sbuf.bufsize - start;
    } else {
       *domain_len = end-start;
    }
    return start;
}

#define SCANNER "scan_email"

%}

%option noyywrap
%option 8bit
%option batch
%option case-insensitive
%option pointer
%option noyymore
%option prefix="yyemail_"

SPECIALS	[()<>@,;:\\".\[\]]
ATOMCHAR	([a-zA-Z0-9`~!#$%^&*\-_=+{}|?])
ATOM		({ATOMCHAR}{1,80})
INUM		(([0-9])|([0-9][0-9])|(1[0-9][0-9])|(2[0-4][0-9])|(25[0-5]))
HEX		([0-9a-f])
XPC		[ !#$%&'()*+,\-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ\[\\\]^_`abcdefghijklmnopqrstuvwxyz{|}~]
PC		[ !#$%&'()*+,\-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ\[\\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"]
ALNUM		[a-zA-Z0-9]
TLD		(AC|AD|AE|AERO|AF|AG|AI|AL|AM|AN|AO|AQ|AR|ARPA|AS|ASIA|AT|AU|AW|AX|AZ|BA|BB|BD|BE|BF|BG|BH|BI|BIZ|BJ|BL|BM|BN|BO|BR|BS|BT|BV|BW|BY|BZ|CA|CAT|CC|CD|CF|CG|CH|CI|CK|CL|CM|CN|CO|COM|COOP|CR|CU|CV|CX|CY|CZ|DE|DJ|DK|DM|DO|DZ|EC|EDU|EE|EG|EH|ER|ES|ET|EU|FI|FJ|FK|FM|FO|FR|GA|GB|GD|GE|GF|GG|GH|GI|GL|GM|GN|GOV|GP|GQ|GR|GS|GT|GU|GW|GY|HK|HM|HN|HR|HT|HU|ID|IE|IL|IM|IN|INFO|INT|IO|IQ|IR|IS|IT|JE|JM|JO|JOBS|JP|KE|KG|KH|KI|KM|KN|KP|KR|KW|KY|KZ|LA|LB|LC|LI|LK|LR|LS|LT|LU|LV|LY|MA|MC|MD|ME|MF|MG|MH|MIL|MK|ML|MM|MN|MO|MOBI|MP|MQ|MR|MS|MT|MU|MUSEUM|MV|MW|MX|MY|MZ|NA|NAME|NC|NE|NET|NF|NG|NI|NL|NO|NP|NR|NU|NZ|OM|ORG|PA|PE|PF|PG|PH|PK|PL|PM|PN|PR|PRO|PS|PT|PW|PY|QA|RE|RO|RS|RU|RW|SA|SB|SC|SD|SE|SG|SH|SI|SJ|SK|SL|SM|SN|SO|SR|ST|SU|SV|SY|SZ|TC|TD|TEL|TF|TG|TH|TJ|TK|TL|TM|TN|TO|TP|TR|TRAVEL|TT|TV|TW|TZ|UA|UG|UK|UM|US|UY|UZ|VA|VC|VE|VG|VI|VN|VU|WF|WS|YE|YT|YU|ZA|ZM|ZW)


DOMAINREF	{ATOM}
SUBDOMAIN	{DOMAINREF}
DOMAIN		({SUBDOMAIN}[.])*{SUBDOMAIN}
QTEXT		[a-zA-Z0-9`~!@#$%^&*()_\-+=\[\]{}\\|;:',.<>/?]
QUOTEDSTRING	["]{QTEXT}*["]
WORD		({ATOM})|({QUOTEDSTRING})
LOCALPART	{WORD}([.]{WORD})*
ADDRSPEC	{LOCALPART}@{DOMAIN}
MAILBOX		{ADDRSPEC}

EMAIL	{ALNUM}[a-zA-Z0-9._%\-+]{1,128}{ALNUM}@{ALNUM}[a-zA-Z0-9._%\-]{1,128}\.{TLD}
YEAR		((19[6-9][0-9])|(20[0-1][0-9]))
DAYOFWEEK	(Mon|Tue|Wed|Thu|Fri|Sat|Sun)
MONTH		(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)
ABBREV		(UT|GMT|EST|EDT|CST|CDT|MST|MDT|PST|PDT|Z|A|M|N|Y)

U_TLD1		(A\0C\0|A\0D\0|A\0E\0|A\0E\0R\0O\0|A\0F\0|A\0G\0|A\0I\0|A\0L\0|A\0M\0|A\0N\0|A\0O\0|A\0Q\0|A\0R\0|A\0R\0P\0A\0|A\0S\0|A\0S\0I\0A\0|A\0T\0|A\0U\0|A\0W\0|A\0X\0|A\0Z\0|B\0A\0|B\0B\0|B\0D\0|B\0E\0|B\0F\0|B\0G\0|B\0H\0|B\0I\0|B\0I\0Z\0|B\0J\0|B\0L\0|B\0M\0|B\0N\0|B\0O\0|B\0R\0|B\0S\0|B\0T\0|B\0V\0|B\0W\0|B\0Y\0|B\0Z\0|C\0A\0|C\0A\0T\0|C\0C\0|C\0D\0|C\0F\0|C\0G\0|C\0H\0|C\0I\0|C\0K\0|C\0L\0|C\0M\0|C\0N\0|C\0O\0|C\0O\0M\0|C\0O\0O\0P\0|C\0R\0|C\0U\0|C\0V\0|C\0X\0|C\0Y\0|C\0Z\0)
U_TLD2		(D\0E\0|D\0J\0|D\0K\0|D\0M\0|D\0O\0|D\0Z\0|E\0C\0|E\0D\0U\0|E\0E\0|E\0G\0|E\0H\0|E\0R\0|E\0S\0|E\0T\0|E\0U\0|F\0I\0|F\0J\0|F\0K\0|F\0M\0|F\0O\0|F\0R\0|G\0A\0|G\0B\0|G\0D\0|G\0E\0|G\0F\0|G\0G\0|G\0H\0|G\0I\0|G\0L\0|G\0M\0|G\0N\0|G\0O\0V\0|G\0P\0|G\0Q\0|G\0R\0|G\0S\0|G\0T\0|G\0U\0|G\0W\0|G\0Y\0|H\0K\0|H\0M\0|H\0N\0|H\0R\0|H\0T\0|H\0U\0|I\0D\0|I\0E\0|I\0L\0|I\0M\0|I\0N\0|I\0N\0F\0O\0|I\0N\0T\0|I\0O\0|I\0Q\0|I\0R\0|I\0S\0|I\0T\0|J\0E\0|J\0M\0|J\0O\0|J\0O\0B\0S\0|J\0P\0|K\0E\0|K\0G\0|K\0H\0|K\0I\0|K\0M\0|K\0N\0|K\0P\0|K\0R\0|K\0W\0|K\0Y\0|K\0Z\0)
U_TLD3		(L\0A\0|L\0B\0|L\0C\0|L\0I\0|L\0K\0|L\0R\0|L\0S\0|L\0T\0|L\0U\0|L\0V\0|L\0Y\0|M\0A\0|M\0C\0|M\0D\0|M\0E\0|M\0F\0|M\0G\0|M\0H\0|M\0I\0L\0|M\0K\0|M\0L\0|M\0M\0|M\0N\0|M\0O\0|M\0O\0B\0I\0|M\0P\0|M\0Q\0|M\0R\0|M\0S\0|M\0T\0|M\0U\0|M\0U\0S\0E\0U\0M\0|M\0V\0|M\0W\0|M\0X\0|M\0Y\0|M\0Z\0|N\0A\0|N\0A\0M\0E\0|N\0C\0|N\0E\0|N\0E\0T\0|N\0F\0|N\0G\0|N\0I\0|N\0L\0|N\0O\0|N\0P\0|N\0R\0|N\0U\0|N\0Z\0|O\0M\0|O\0R\0G\0|P\0A\0|P\0E\0|P\0F\0|P\0G\0|P\0H\0|P\0K\0|P\0L\0|P\0M\0|P\0N\0|P\0R\0|P\0R\0O\0|P\0S\0|P\0T\0|P\0W\0|P\0Y\0)
U_TLD4		(Q\0A\0|R\0E\0|R\0O\0|R\0S\0|R\0U\0|R\0W\0|S\0A\0|S\0B\0|S\0C\0|S\0D\0|S\0E\0|S\0G\0|S\0H\0|S\0I\0|S\0J\0|S\0K\0|S\0L\0|S\0M\0|S\0N\0|S\0O\0|S\0R\0|S\0T\0|S\0U\0|S\0V\0|S\0Y\0|S\0Z\0|T\0C\0|T\0D\0|T\0E\0L\0|T\0F\0|T\0G\0|T\0H\0|T\0J\0|T\0K\0|T\0L\0|T\0M\0|T\0N\0|T\0O\0|T\0P\0|T\0R\0|T\0R\0A\0V\0E\0L\0|T\0T\0|T\0V\0|T\0W\0|T\0Z\0|U\0A\0|U\0G\0|U\0K\0|U\0M\0|U\0S\0|U\0Y\0|U\0Z\0|V\0A\0|V\0C\0|V\0E\0|V\0G\0|V\0I\0|V\0N\0|V\0U\0|W\0F\0|W\0S\0|Y\0E\0|Y\0T\0|Y\0U\0|Z\0A\0|Z\0M\0|Z\0W\0)

%%



{DAYOFWEEK},[ \t\x0A\x0D]+[0-9]{1,2}[ \t\x0A\x0D]+{MONTH}[ \t\x0A\x0D]+{YEAR}[ \t\x0A\x0D]+[0-2][0-9]:[0-5][0-9]:[0-5][0-9][ \t\x0A\x0D]+([+-][0-2][0-9][0314][05]|{ABBREV}) {
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    s.rfc822_recorder.write_buf(SBUF,POS,yyleng);
    s.pos += yyleng;

    /************
     *** NOTE ***
     ************
     *
     * NEVER modify yyleng, or else pos will not reflect the current position in the buffer.
     */
}

Message-ID:([ \t\x0A]|\x0D\x0A)?<{PC}{1,80}> {
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    s.rfc822_recorder.write_buf(SBUF,POS,yyleng);
    s.pos += yyleng;
}

Subject:[ \t]?({PC}{1,80}) {
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    s.rfc822_recorder.write_buf(SBUF,POS,yyleng);
    s.pos += yyleng;
}

Cookie:[ \t]?({PC}{1,80}) {
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    s.rfc822_recorder.write_buf(SBUF,POS,yyleng);
    s.pos += yyleng;
}

Host:[ \t]?([a-zA-Z0-9._]{1,64}) {
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    s.rfc822_recorder.write_buf(SBUF,POS,yyleng);
    s.pos += yyleng;
}

{EMAIL}/[^a-zA-Z]	{
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    if (extra_validate_email(yytext)){
        s.email_recorder.write_buf(SBUF,POS,yyleng);
	ssize_t domain_start = find_host_in_email(SBUF.slice(POS,yyleng));
        if (domain_start>0){
            s.domain_recorder.write_buf(SBUF, POS+domain_start,yyleng-domain_start);
        }
    }
    s.pos += yyleng;
}


{INUM}?\.{INUM}\.{INUM}\.{INUM}\.{INUM}	{
    /* Not an IP address, but could generate a false positive */
    // printf("IP Address False Positive at 1 '%s'\n",yytext);
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    s.pos += yyleng;
}

[0-9][0-9][0-9][0-9]\.{INUM}\.{INUM}\.{INUM}	{
    /* Also not an IP address, but could generate a false positive */
    // printf("IP Address False Positive at 2 '%s'\n",yytext);
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    s.pos += yyleng;
}


{INUM}\.{INUM}\.{INUM}\.{INUM}/[^0-9\-\.\+A-Z_] {
    // UTF-8 IPv4 address canner
    /* Numeric IP addresses. Get the context before and throw away some things */
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    int ignore=0;

    /* Get 8 characters of left context, right-justified */
    int context_len = 8;
    int c0 = POS - context_len;
    while(c0<0){
       c0++;
       context_len--;
    }
    std::string context = SBUF.substr(c0,context_len);
    while(context.size()<8){
        context = std::string(" ")+context;
    }

    /* Now have some rules for ignoring */
    if (isalnum(context[7])) ignore=1;
    if (context[7]=='.' || context[7]=='-' || context[7]=='+') ignore=1;
    if (ishexnumber(context[4]) && ishexnumber(context[5]) && ishexnumber(context[6]) && context[7]=='}') ignore=1;

    if (context.find("v.",5) != std::string::npos) ignore=1;
    if (context.find("v ",5) != std::string::npos) ignore=1;
    if (context.find("rv:",5) != std::string::npos) ignore=1;       /* rv:1.9.2.8 as in Mozilla */

    if (context.find(">=",4) != std::string::npos) ignore=1;   /* >= 1.8.0.10 */
    if (context.find("<=",4) != std::string::npos) ignore=1;   /* <= 1.8.0.10 */
    if (context.find("<<",4) != std::string::npos) ignore=1;   /* <= 1.8.0.10 */

    if (context.find("ver",4) != std::string::npos) ignore=1;
    if (context.find("Ver",4) != std::string::npos) ignore=1;
    if (context.find("VER",4) != std::string::npos) ignore=1;

    if (context.find("rsion") != std::string::npos) ignore=1;
    if (context.find("ion=")  != std::string::npos) ignore=1;
    if (context.find("PSW/")  != std::string::npos) ignore=1;  /* PWS/1.5.19.3 ... */

    if (context.find("flash=") != std::string::npos) ignore=1;   /* flash= */
    if (context.find("stone=") != std::string::npos) ignore=1;   /* Milestone= */
    if (context.find("NSS",4)  != std::string::npos) ignore=1;

    if (context.find("/2001,") != std::string::npos) ignore=1;     /* /2001,3.60.50.8 */
    if (context.find("TI_SZ") != std::string::npos) ignore=1;     /* %REG_MULTI_SZ%, */

    /* Ignore 0. */
    if (SBUF[POS]=='0' && SBUF[POS+1]=='.') ignore=1;

    if (!ignore) {
        s.domain_recorder.write_buf(SBUF,POS,yyleng);
    }
    s.pos += yyleng;
}

[^0-9A-Z:]{HEX}{HEX}:{HEX}{HEX}:{HEX}{HEX}:{HEX}{HEX}:{HEX}{HEX}:{HEX}{HEX}/[^0-9A-Z:] {
    // UTF-8 Ethernet scanner
    /* found a possible ethernet address! */
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    if (s.valid_ether_addr(POS+1)){
       s.ether_recorder.write_buf(SBUF,POS+1,yyleng-1);
    }
    s.pos += yyleng;
}

((https?)|afp|smb):\/\/[a-zA-Z0-9_%/\-+@:=&\?#~.;]{1,384}	{
    // UTF-8 http scanner
    // for reasons that aren't clear, there are a lot of net protocols that have an http://domain
    // in them followed by numbers. So this counts the number of slashes and if it is only 2
    // the size is pruned until the last character is a letter
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    int slash_count = 0;
    int feature_len = yyleng;
    for (unsigned int i=0;i<(unsigned int)yyleng;i++){
        if (SBUF[POS+i]=='/') slash_count++;
    }
    if (slash_count==2){
       while(feature_len>0 && !isalpha(SBUF[POS+feature_len-1])){
         feature_len--;
       }
    }
    s.url_recorder.write_buf(SBUF,POS,feature_len);                // record the URL
    size_t domain_len=0;
    ssize_t domain_start = find_host_in_url(SBUF.slice(POS,feature_len), &domain_len);  // find the start of domain?
    if (domain_start >= 0 && domain_len > 0){
	s.domain_recorder.write_buf(SBUF,POS+domain_start,domain_len);
    }
    s.pos += yyleng;
}

[a-zA-Z0-9]\0([a-zA-Z0-9._%\-+]\0){1,128}@\0([a-zA-Z0-9._%\-]\0){1,128}\.\0({U_TLD1}|{U_TLD2}|{U_TLD3}|{U_TLD4})/[^a-zA-Z]|([^][^\0])	{
    /* UTF-16 URL scanner */
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    if (extra_validate_email(yytext)){
        s.email_recorder.write_buf(SBUF,POS,yyleng);
        ssize_t domain_start = find_host_in_email(SBUF.slice(POS,yyleng)) + 1;
        if (domain_start >= 0){
            s.domain_recorder.write_buf(SBUF,POS+domain_start,yyleng-domain_start);
        }
    }
    s.pos += yyleng;
}

h\0t\0t\0p\0(s\0)?:\0([a-zA-Z0-9_%/\-+@:=&\?#~.;]\0){1,128}/[^a-zA-Z0-9_%\/\-+@:=&\?#~.;]|([^][^\0])	{
    /* UTF-16 URL scanner */
    email_scanner &s = * yyemail_get_extra(yyscanner);
    s.check_margin();
    s.url_recorder.write_buf(SBUF,POS,yyleng);
    ssize_t domain_start = find_host_in_email(SBUF.slice(POS,yyleng));
    if (domain_start >= 0){
	s.domain_recorder.write_buf(SBUF,POS+domain_start,yyleng-domain_start);
    }
    s.pos += yyleng;
}

.|\n {
    /**
     * The no-match rule. VERY IMPORTANT!
     * If we are beyond the end of the margin, call it quits.
     */
    email_scanner &s = *yyemail_get_extra(yyscanner);
    s.check_margin();
    s.pos++;
}
%%

extern "C"
void scan_email(struct scanner_params &sp)
{
    sp.check_version();
    if (sp.phase==scanner_params::PHASE_INIT){
        sp.info->set_name("email");
        sp.info->author            = "Simson L. Garfinkel";
        sp.info->description       = "Scans for email addresses, domains, URLs, RFC822 headers, etc.";
        sp.info->scanner_version   = "1.1";

	/* define the feature files this scanner created */
        sp.info->feature_defs.push_back( feature_recorder_def("email"));
        sp.info->feature_defs.push_back( feature_recorder_def("domain"));
        sp.info->feature_defs.push_back( feature_recorder_def("url"));
        sp.info->feature_defs.push_back( feature_recorder_def("rfc822"));
        sp.info->feature_defs.push_back( feature_recorder_def("ether"));

	/* define the histograms to make */
        auto no_flags  = histogram_def::flags_t();
        auto lowercase = histogram_def::flags_t(); lowercase.lowercase = true;

	sp.info->histogram_defs.push_back( histogram_def("email1", "email",  "",                                     "", "histogram",lowercase));
	sp.info->histogram_defs.push_back( histogram_def("email2", "email",  "(@.*)",                                "", "domain_histogram",lowercase));
	sp.info->histogram_defs.push_back( histogram_def("email3", "domain", "",                                     "", "histogram", no_flags));
        sp.info->histogram_defs.push_back( histogram_def("url1",   "url",    "",                                     "", "histogram", no_flags));
	sp.info->histogram_defs.push_back( histogram_def("url2",   "url",    "://([^/]+)",                           "", "services", no_flags));
	sp.info->histogram_defs.push_back( histogram_def("url3",   "url",    "://((cid-[0-9a-f])+[a-z.].live.com/)", "", "microsoft-live", no_flags));
	sp.info->histogram_defs.push_back( histogram_def("url4",   "url",    "://[-_a-z0-9.]+facebook.com/.*[&?]{1}id=([0-9]+)","", "facebook-id", no_flags));
	sp.info->histogram_defs.push_back( histogram_def("url5",   "url",    "://[-_a-z0-9.]+facebook.com/([a-zA-Z0-9.]*[^/?&]$)","", "facebook-address",lowercase));
	sp.info->histogram_defs.push_back( histogram_def("url6",   "url",    "search.*[?&/;fF][pq]=([^&/]+)",       "", "searches", no_flags));

        sp.info->histogram_defs.push_back( histogram_def("ether","ether", "([^\(]+)","", "histogram", histogram_def::flags_t()));
	return;
    }
    if (sp.phase==scanner_params::PHASE_SCAN){

	/* Set up the buffer. Scan it. Exit */
        email_scanner lexer(sp);
	yyscan_t scanner;
        yyemail_lex_init(&scanner);
        yyemail_set_extra(&lexer, scanner);
        try {
            yyemail_lex(scanner);
        }
        catch (sbuf_scanner::sbuf_scanner_exception &e ) {
            std::cerr << "Scanner " << SCANNER << "Exception " << e.what() << " processing " << sp.sbuf->pos0 << "\n";
        }
        yyemail_lex(scanner);           // cleanup at end
        yyemail_lex_destroy(scanner);
	(void)yyunput;			// avoids defined but not used
    }
}
