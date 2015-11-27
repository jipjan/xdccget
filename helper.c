#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <sys/ioctl.h>
#include <inttypes.h>

#include "helper.h"
#include "file.h"

struct terminalDimension td;

static inline void clear_bit(bitset_t *x, int bitNum) {
    *x &= ~(1L << bitNum);
}

static inline void set_bit(bitset_t *x, int bitNum) {
    *x |= (1L << bitNum);
}

static inline int get_bit(bitset_t *x, int bitNum) {
    int bit = 0;
    bit = (*x >> bitNum) & 1L;
    return bit;
}

inline void cfg_clear_bit(struct xdccGetConfig *config, int bitNum) {
    clear_bit(&config->flags, bitNum);
}

inline void cfg_set_bit(struct xdccGetConfig *config, int bitNum) {
    set_bit(&config->flags, bitNum);
}

inline int cfg_get_bit(struct xdccGetConfig *config, int bitNum) {
    return get_bit(&config->flags, bitNum);
}

static inline void logprintf_line (FILE *stream, char *color_code, char *prefix, char *formatString, va_list va_alist) {
    fprintf(stream, "%s[%s] - ", color_code, prefix);
    vfprintf(stream, formatString, va_alist);
    fprintf(stream, "%s\n", KNRM);
}

void logprintf(int logLevel, char *formatString, ...) {
    va_list va_alist;
    struct xdccGetConfig *cfg = getCfg();

    va_start(va_alist, formatString);

    switch (logLevel) {
    case LOG_QUIET:
        if (cfg->logLevel >= LOG_QUIET) {
			logprintf_line(stdout, KBLU, "Quiet", formatString, va_alist);
        }
        break;
    case LOG_INFO:
        if (cfg->logLevel >= LOG_INFO) {
			logprintf_line(stdout, KGRN, "Info", formatString, va_alist);
        }
        break;
    case LOG_WARN:
        if (cfg->logLevel >= LOG_WARN) {
			logprintf_line(stderr, KYEL, "Warning", formatString, va_alist);
        }
        break;
    case LOG_ERR:
        if (cfg->logLevel >= LOG_ERR) {
			logprintf_line(stderr, KRED, "Error", formatString, va_alist);
        }
        break;
    default:
        DBG_WARN("logprintf called with unknown log-level. using normal logging.");
        vfprintf(stdout, formatString, va_alist);
        fprintf(stdout, "\n");
        break;
    }

    va_end(va_alist);
}

void initRand() {
    time_t t = time(NULL);
	
	if (t == ((time_t) -1)) {
		DBG_ERR("time failed");
	}
	
    srand((unsigned int) t);
}

struct TextReaderContext {
    sds content;
};

static void TextReaderCallback (void *buffer, unsigned int bytesRead, void *ctx) {
    char *buf = buffer;
    struct TextReaderContext *context = ctx;
    buf[bytesRead] = (char) 0;
    context->content =  sdscatprintf(context->content, "%s", buf);
}

sds readTextFile(char *filePath) {
    struct TextReaderContext context;
    context.content = sdsnew("");

    readFile(filePath, TextReaderCallback, &context);

    return context.content;
}

int rand_range(int low, int high) {
    if (high == 0) {
        return 0;
    }
    return (rand() % high + low);
}

void createRandomNick(int nickLen, char *nick) {
    char *possibleChars = "abcdefghiklmnopqrstuvwxyzABCDEFGHIJHKLMOPQRSTUVWXYZ";
    size_t numChars = strlen(possibleChars);
    int i;

    if (nick == NULL) {
        DBG_WARN("nick = NULL!");
        return;
    }

    for (i = 0; i < nickLen; i++) {
        nick[i] = possibleChars[rand_range(0, numChars - 1)];
    }

}

struct terminalDimension *getTerminalDimension() {
    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);

    td.rows = w.ws_row;
    td.cols = w.ws_col;
    return &td;
}

void printProgressBar(const int numBars, const double percentRdy) {
    const int NUM_BARS = numBars;
    int i = 0;

    putchar('[');

    for (i = 0; i < NUM_BARS; i++) {
        if (i < (int) (NUM_BARS * percentRdy)) {
            putchar('#');
        }
        else {
            putchar('-');
        }
    }

    putchar(']');
}

int printSize(irc_dcc_size_t size) {
    char *sizeNames[] = {"Byte", "KByte", "MByte", "GByte", "TByte", "PByte"};

    double temp = (double) size;
    int i = 0;

    while (temp > 1024) {
        temp /= 1024;
        i++;
    }

    int charsPrinted = 0;

    if (i >= (sizeof (sizeNames) / sizeof (char*))) {
        charsPrinted = printf("%" IRC_DCC_SIZE_T_FORMAT " Byte", size);
    }
    else {
        charsPrinted = printf("%0.3f %s", temp, sizeNames[i]);
    }

    return charsPrinted;
}

int printETA(double seconds) {
    int charsPrinted = 0;
    if (seconds <= 60) {
        charsPrinted = printf("%.0fs", seconds);
    }
    else {
        double mins = seconds / 60;
        double hours = mins / 60;
        double remainMins = mins - ((unsigned int) hours) * 60;
        double days = hours / 24;
        double remainHours = hours - ((unsigned int) days) * 24;
        double remainSeconds = seconds - ((unsigned int) mins) *60;

        if (days >= 1) {
            charsPrinted += printf("%.0fd", days);
        }

        if (remainHours >= 1) {
            charsPrinted += printf("%.0fh", remainHours);
        }

        charsPrinted += printf("%.0fm%.0fs", remainMins, remainSeconds);
    }
    return charsPrinted;
}

void outputProgress(struct dccDownloadProgress *progress) {
    struct terminalDimension *terminalDimension = getTerminalDimension();
    /* see comments below how these "numbers" are calculated */
    int progBarLen = terminalDimension->cols - (8 + 14 + 1 + 14 + 1 + 14 + 3 + 13 /* +1 for windows...*/);

    progress->sizeLast = progress->sizeNow;
    progress->sizeNow = progress->sizeRcvd;

    irc_dcc_size_t temp = (progress->completeFileSize == 0) ? 0 : progress->sizeRcvd * 1000000L / progress->completeFileSize;
    double curProcess = (double) temp / 1000000;
    //double curProcess = (tdp->completeFileSize == 0) ? 0 : ((double)tdp->sizeRcvd / (double) tdp->completeFileSize);
    irc_dcc_size_t curSpeed = progress->sizeNow - progress->sizeLast;

    int printedChars = progBarLen + 2;

    printProgressBar(progBarLen, curProcess);
    /* 8 chars -->' 75.30% ' */
    printedChars += printf(" %.2f%% ", curProcess * 100);
    /* 14 chars --> '1001.132 MByte' */
    printedChars += printSize(progress->sizeRcvd);
    /* 1 char */
    printedChars += printf("/");
    /* 14 chars --> '1001.132 MByte' */
    printedChars += printSize(progress->completeFileSize);
    /*printf (" , Downloading %s", tdp->fileName);*/
    /* 1 char */
    printedChars += printf("|");
    /* 14 chars --> '1001.132 MByte' */
    printedChars += printSize(curSpeed);
    /* 3 chars */
    printedChars += printf("/s|");

    /*calc ETA - max 13 chars */
    irc_dcc_size_t remainingSize = progress->completeFileSize - progress->sizeRcvd;
    if (remainingSize > 0 && curSpeed > 0) {
        double etaSeconds = ((double) remainingSize / (double) curSpeed);
        printedChars += printETA(etaSeconds);
    }
    else {
        printedChars += printf("---");
    }

    /* fill remaining columns of terminal with spaces, in ordner to clean the output... */

    int j;
    for (j = printedChars; j < terminalDimension->cols - 1; j++) {
        printf(" ");
    }
}

#ifdef ENABLE_SSL

static const char* get_validation_errstr(long e) {
    switch ((int) e) {
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
        return "ERR_UNABLE_TO_GET_ISSUER_CERT";
    case X509_V_ERR_UNABLE_TO_GET_CRL:
        return "ERR_UNABLE_TO_GET_CRL";
    case X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE:
        return "ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE";
    case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE:
        return "ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE";
    case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY:
        return "ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY";
    case X509_V_ERR_CERT_SIGNATURE_FAILURE:
        return "ERR_CERT_SIGNATURE_FAILURE";
    case X509_V_ERR_CRL_SIGNATURE_FAILURE:
        return "ERR_CRL_SIGNATURE_FAILURE";
    case X509_V_ERR_CERT_NOT_YET_VALID:
        return "ERR_CERT_NOT_YET_VALID";
    case X509_V_ERR_CERT_HAS_EXPIRED:
        return "ERR_CERT_HAS_EXPIRED";
    case X509_V_ERR_CRL_NOT_YET_VALID:
        return "ERR_CRL_NOT_YET_VALID";
    case X509_V_ERR_CRL_HAS_EXPIRED:
        return "ERR_CRL_HAS_EXPIRED";
    case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
        return "ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD";
    case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
        return "ERR_ERROR_IN_CERT_NOT_AFTER_FIELD";
    case X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD:
        return "ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD";
    case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD:
        return "ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD";
    case X509_V_ERR_OUT_OF_MEM:
        return "ERR_OUT_OF_MEM";
    case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
        return "ERR_DEPTH_ZERO_SELF_SIGNED_CERT";
    case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
        return "ERR_SELF_SIGNED_CERT_IN_CHAIN";
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
        return "ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY";
    case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
        return "ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE";
    case X509_V_ERR_CERT_CHAIN_TOO_LONG:
        return "ERR_CERT_CHAIN_TOO_LONG";
    case X509_V_ERR_CERT_REVOKED:
        return "ERR_CERT_REVOKED";
    case X509_V_ERR_INVALID_CA:
        return "ERR_INVALID_CA";
    case X509_V_ERR_PATH_LENGTH_EXCEEDED:
        return "ERR_PATH_LENGTH_EXCEEDED";
    case X509_V_ERR_INVALID_PURPOSE:
        return "ERR_INVALID_PURPOSE";
    case X509_V_ERR_CERT_UNTRUSTED:
        return "ERR_CERT_UNTRUSTED";
    case X509_V_ERR_CERT_REJECTED:
        return "ERR_CERT_REJECTED";
    case X509_V_ERR_SUBJECT_ISSUER_MISMATCH:
        return "ERR_SUBJECT_ISSUER_MISMATCH";
    case X509_V_ERR_AKID_SKID_MISMATCH:
        return "ERR_AKID_SKID_MISMATCH";
    case X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH:
        return "ERR_AKID_ISSUER_SERIAL_MISMATCH";
    case X509_V_ERR_KEYUSAGE_NO_CERTSIGN:
        return "ERR_KEYUSAGE_NO_CERTSIGN";
    case X509_V_ERR_INVALID_EXTENSION:
        return "ERR_INVALID_EXTENSION";
    case X509_V_ERR_INVALID_POLICY_EXTENSION:
        return "ERR_INVALID_POLICY_EXTENSION";
    case X509_V_ERR_NO_EXPLICIT_POLICY:
        return "ERR_NO_EXPLICIT_POLICY";
    case X509_V_ERR_APPLICATION_VERIFICATION:
        return "ERR_APPLICATION_VERIFICATION";
    default:
        return "ERR_UNKNOWN";
    }
}


int openssl_check_certificate_callback(int preverify_ok, X509_STORE_CTX *ctx) {
    SSL *ssl;
    X509* cert = ctx->cert;
    struct xdccGetConfig *cfg = getCfg();

    ssl = X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());

    char *subj = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
    char *issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
    
    logprintf(LOG_INFO, "Got the following certificate:");
    logprintf(LOG_INFO, "%s", subj);
    logprintf(LOG_INFO, "The issuer was:");
    logprintf(LOG_INFO, "%s", issuer);
    
    if (preverify_ok == 0) {
        logprintf(LOG_ERR, "There was a problem with the server certificate:");
        logprintf(LOG_ERR, "%s", get_validation_errstr(ctx->error));
    }
    
    free(subj);
    free(issuer);
    
    if (cfg_get_bit(cfg, ALLOW_ALL_CERTS_FLAG))
        return 1;
    else
        return preverify_ok;
}

#endif
