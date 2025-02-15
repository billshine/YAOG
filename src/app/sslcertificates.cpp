#include "sslcertificates.h"
#ifdef _WIN32
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  #include <openssl\applink.c>
#else
  #include <openssl\applink.c>
#endif
#endif
#include <string.h>

#include <openssl/ossl_typ.h>

#ifdef Q_OS_WIN
#include <windows.h> // for Sleep
#endif

#if defined _MSC_VER
/* DISABLE WARNINGS */
#define DISABLE_WARNINGS __pragma(warning( push, 0 ))
/* ENABLE WARNINGS */
#define ENABLE_WARNINGS __pragma(warning( pop ))
#elif defined __GNUC__
/* DISABLE WARNINGS */
    #define DISABLE_WARNINGS \
        _Pragma("GCC diagnostic push") \
        _Pragma("GCC diagnostic ignored \"-Wall\"")
/* ENABLE WARNINGS */
    #define ENABLE_WARNINGS _Pragma("GCC diagnostic push")
#else  /* TODO: add more compilers here */
    #define DW
    #define EW
#endif

void(*SSLCertificates::output_display)(const char*)=nullptr;
int SSLCertificates::abortnow=0;

SSLCertificates::SSLCertificates()
{
    this->keyLength=1024;
    this->x509=nullptr;
    this->pkey=nullptr;

    if ((this->pkey=EVP_PKEY_new()) == nullptr)    throw 10;
    if ((this->x509=X509_new())     == nullptr)    throw 20;
    if ((this->rsakey=RSA_new())    == nullptr)    throw 10;
    if ((this->csr=X509_REQ_new())  == nullptr)    throw 10;
    if ((this->eckey=EC_KEY_new())  == nullptr)    throw 10;
    if ((this->dsakey=DSA_new())    == nullptr)    throw 10;

    this->output_display=nullptr;
    //CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);
    this->bio_err=BIO_new_fp(stdout, BIO_NOCLOSE);

    // OpenSSL load of quite everything.
    ERR_load_crypto_strings (); // Load error strings
    SSL_load_error_strings(); // Load errors (as ssl app)
    OpenSSL_add_all_algorithms(); // Add all encryption algo
    OpenSSL_add_all_digests(); // Add all digests
    OpenSSL_add_all_ciphers(); // all ciphers
    
    SSL_library_init();

    SSLCertificates::abortnow=0;
    this->SSLErrorNum=-1;
    this->SSLError=0;
    this->set_digest(const_cast <char*>("sha256"));
    this->subjectNum=0;
    this->keyType=0;
    this->set_cipher(const_cast <char*>("aes256"));
    this->startDate=this->endDate=nullptr;
    this->serialnum=1;
    this->extensionNum=0;
    this->p12Name=nullptr;
    this->ca=nullptr;
    this->caListCerts.clear();
    this->caListCN.clear();

    //TODO : random init
}

SSLCertificates::~SSLCertificates()
{
    if (this->x509 != nullptr) X509_free(this->x509);
    if (this->pkey != nullptr) EVP_PKEY_free(this->pkey);
    if (this->csr  != nullptr) X509_REQ_free(this->csr);
    // TODO : (Memory leak)
    // EVP_PKEY_free also free the RSA or EC or DSA : must check which one is used before free
    //if (this->rsakey != nullptr) RSA_free(this->rsakey);
    //if (this->eckey != nullptr) EC_KEY_free(this->eckey);
    if (this->startDate != nullptr) ASN1_TIME_free(this->startDate);
    if (this->endDate != nullptr) ASN1_TIME_free(this->endDate);
    for (int i=0; i<this->subjectNum;i++)
    {
        if (this->subject_id[i]!=nullptr) free(this->subject_id[i]);
        if (this->subject[i]!=nullptr) free(this->subject[i]);
    }
    //TODO clear also extensions

#ifndef OPENSSL_NO_ENGINE
    ENGINE_cleanup();
#endif
    CRYPTO_cleanup_all_ex_data();

    // TODO : this writes mem leaks with 1.0.2, not found in 1.1 -> check new
    //CRYPTO_mem_leaks(this->bio_err);
    BIO_free(this->bio_err);
    if (p12Name != nullptr) free(p12Name);
    if (ca!=nullptr) sk_X509_free(ca);
    caListCN.clear();
    for (std::vector<X509*>::iterator x509 = caListCerts.begin();
         x509 != caListCerts.end(); ++x509)
    {
      X509_free(*x509);
    }
    caListCerts.clear();

    RAND_cleanup();
}

/*************************** Display and errors functions ************************/

void SSLCertificates::print_ssl_errors(char* buffer,size_t size)
{
    char linebuf[200]=""; // TODO : dynamic size with max size
    if (this->SSLErrorNum == -1)
    {
        strcpy(buffer,"No current SSL errors ");
        return;
    }
    buffer[0]='\0';
    for (int i=0;i<=this->SSLErrorNum;i++)
    {
       sprintf(linebuf,"Lib : %s, doing : %s, reason : %s (%i/%i)\n",
               ERR_lib_error_string(this->SSLErrorList[i]),
               ERR_func_error_string((this->SSLErrorList[i])),
               ERR_reason_error_string((this->SSLErrorList[i])),
               ERR_GET_LIB(this->SSLErrorList[i]),
               ERR_GET_REASON(this->SSLErrorList[i]));
       if ((strlen (buffer)+strlen (linebuf)) > size)
       {
           strcat(buffer,"[...]");
           this->SSLErrorNum=-1;
           return;
       }
       strcat(buffer,linebuf);
    }
    this->SSLErrorNum=-1;
}

void SSLCertificates::get_ssl_errors()
{
    unsigned long int errcode;

    while ((errcode= ERR_get_error())!= 0) {
        this->SSLError=1;
        this->SSLErrorList[++this->SSLErrorNum]=errcode;
        if (this->SSLErrorNum >= MAX_SSL_ERRORS) this->SSLErrorNum=0; //cycle on overflow
    }
}

void SSLCertificates::empty_ssl_errors()
{
    this->SSLErrorNum=-1;
    this->SSLError=0;
}

void SSLCertificates::set_display_callback(void (*callback)(const char *))
{
    SSLCertificates::output_display=callback;
}

void SSLCertificates::clear_display_callback()
{
   SSLCertificates::output_display=nullptr;
}

/*************************** Certificate functions ************************/

int SSLCertificates::add_cert_object_byname(const char* label,const char* content)
{
   // Just to check it is valid
    /*
    X509_NAME2* X509_name= X509_get_subject_name(this->x509);
   if (X509_NAME_add_entry_by_txt(X509_name,label,
                                  MBSTRING_ASC,
                                  content,
                                  -1,   // auto length
                                  -1,   // loc
                                  0     // set
                                  ) == 0)
   {
       this->get_ssl_errors();
       return 1;
   }
*/
   this->subject_id[this->subjectNum] = strdup(label);
   this->subject[this->subjectNum] = reinterpret_cast<unsigned char*>( strdup(content));
   this->subjectNum++;

   return 0;
}

int SSLCertificates::set_object(const char* oCN, const char* oC, const char* oS,
               const char* oL, const char* oO, const char* oOU,
               const char *omail)
{
    if (strcmp(oCN,"")!=0)
    {
        if (strcmp(omail,"")!=0)
        {
            std::string newCN=oCN;
            newCN += "/emailAddress=";
            newCN += omail;
            if (this->add_cert_object_byname("CN",static_cast<const char*>(newCN.data())) != 0) return 1;
        }
        else
            if (this->add_cert_object_byname("CN",oCN) != 0) return 1;
    }
    else
        return 1;
    if (strcmp(oOU,"")!=0)
        if (this->add_cert_object_byname("OU",oOU) != 0) return 1;
    if (strcmp(oO,"")!=0)
        if (this->add_cert_object_byname("O",oO) != 0) return 1;
    if (strcmp(oL,"")!=0)
        if (this->add_cert_object_byname("L",oL) != 0) return 1;
    if (strcmp(oS,"")!=0)
        if (this->add_cert_object_byname("ST",oS) != 0) return 1;
    if (strcmp(oC,"")!=0)
        if (this->add_cert_object_byname("C",oC) != 0) return 1;


    return 0;
}

int SSLCertificates::set_key_params(unsigned int keyparam, int keytype, std::string ec)
{
    int i;
    switch (keytype)
    {
    case KeyRSA:
        this->keyLength=keyparam;
        this->keyType=keytype;
        break;
    case KeyEC:
        for (i=0;i< keyECListNum; i++)
        {
            //if (!strcmp(ec,this->keyECList[i]))
            if (ec == this->keyECList[i])
            {
                this->keyECType=this->keyECListNIDCode[i];
                this->keyType=keytype;
                return 0;
            }
        }
        return 1;
    case KeyDSA:
        this->keyLength=keyparam;
        this->keyType=keytype;
        break;
    default:
        return 1;
    }
    return 0;
}

int SSLCertificates::set_digest(char *digest)
{
    EVP_MD* digestalg;
    if ((digestalg=const_cast<EVP_MD*>(EVP_get_digestbyname(digest)))==nullptr)
    {
        this->get_ssl_errors();
        return 1;
    }
    this->useDigest=digestalg;
    return 0;
}

int SSLCertificates::set_cipher(char *cipher)
{
    EVP_CIPHER* cipheralg;
    if ((cipheralg=const_cast<EVP_CIPHER*>(EVP_get_cipherbyname(cipher)))==nullptr)
    {
        this->get_ssl_errors();
        return 1;
    }
    this->useCipher=cipheralg;
    return 0;
}

int SSLCertificates::set_X509_validity(char *start,char *end)
{
    this->startDate=ASN1_TIME_new();
    this->endDate=ASN1_TIME_new();

    if (ASN1_TIME_set_string(this->startDate, start)!=1)
    {
        return 1;
    }
    if (ASN1_TIME_set_string(this->endDate, end)!=1)
    {
        return 1;
    }
    return 0;
    /* BUG also with this....
    if (ASN1_TIME_set(this->startDate, start) == nullptr)
       return 1;
    if ((this->endDate = ASN1_TIME_set(NULL, end)) == NULL)
    {
        ASN1_TIME_free(this->startDate);
        return 1;
    }*/
//  TODO : BUG This (ASN1_TIME_diff) makes program crash on debug mode with QT......
//  (SIGSEV at startup : while this is not even called...)
/*
    int day, sec;
    if (!ASN1_TIME_diff(&day, &sec, this->startDate, this->endDate))
    {
        ASN1_TIME_free(this->startDate);
        ASN1_TIME_free(this->endDate);
        return 255; // Why would this happen ?
    }

    if (day < 0 || sec < 0)
    {
       ASN1_TIME_free(this->startDate);
        ASN1_TIME_free(this->endDate);
        return 2;
    }*/
}

void SSLCertificates::set_X509_serial(unsigned int serial)
{
    this->serialnum=serial;
}

int SSLCertificates::create_key()
{

    if (this->output_display != nullptr) this->output_display("Starting key generation\n");

    BIGNUM *f4 = BN_new(); // Init exponent (65537)
    BN_set_word(f4, RSA_F4);
    // Callback structure
#if OPENSSL_VERSION_NUMBER > 0x10100000L
    BN_GENCB* cb = BN_GENCB_new();
#else
    BN_GENCB cbn;
    BN_GENCB* cb=&cbn;
#endif
    BN_GENCB_set(cb,this->callback, nullptr);

    int retcode;

    switch (this->keyType)
    {
    case KeyRSA:
        retcode = RSA_generate_key_ex(this->rsakey, static_cast<int>(this->keyLength), f4,cb);

        BN_free(f4);
        if (retcode != 1)
        {
            this->get_ssl_errors();
            return 3;
        }                   

        if (! EVP_PKEY_assign_RSA(this->pkey,this->rsakey))

        {
            this->get_ssl_errors();
            return 1;
            }
        break;
    case KeyEC:
        if((this->eckey = EC_KEY_new_by_curve_name(this->keyECType))==nullptr)
        {
            this->get_ssl_errors();
            return 1;
        }
        if (EC_KEY_generate_key(this->eckey)!=1)
        {
            this->get_ssl_errors();
            return 1;
        }

        if (EVP_PKEY_assign_EC_KEY(this->pkey,this->eckey) != 1)

        {
            this->get_ssl_errors();
            return 1;
        }
        break;
    case KeyDSA:
        retcode = DSA_generate_parameters_ex(this->dsakey, static_cast<int>(this->keyLength),
                        nullptr,0, // TODO Seed null -> random
                        nullptr, nullptr,// counter unused TODO : check if usefull
                        cb);
        if (retcode == 0)
        {
            this->get_ssl_errors();
            return 1;
        }
        retcode = DSA_generate_key(this->dsakey);
        if (retcode == 0)
        {
            this->get_ssl_errors();
            return 1;
        }

        if (EVP_PKEY_assign_DSA(this->pkey,this->dsakey) != 1)

        {
            this->get_ssl_errors();
            return 1;
        }
        break;
    default:
        if (this->output_display != nullptr) this->output_display("Unknown key type\n");
        return 1;
    }

    if (this->output_display != nullptr) this->output_display("Key generation finished\n");
#if OPENSSL_VERSION_NUMBER > 0x10100000L
    BN_GENCB_free(cb); // TODO : also free in other cases -> change function structure
#endif
    return 0;
}
void print_openssl_errors() {
    unsigned long err;
    while ((err = ERR_get_error()) != 0) {
        char err_msg[256];
        ERR_error_string_n(err, err_msg, sizeof(err_msg));
        printf("OpenSSL Error: %s \n",err_msg);
    }
}

int SSLCertificates::create_cert()
{
    X509_NAME2 *x509Name;
    std::string value;
    int i;

    if (this->output_display != nullptr) this->output_display("Starting certificate generation\n");
    if (X509_set_version(this->x509,2) == 0)
    {
        this->get_ssl_errors();
        return 1;
    }
    if (ASN1_INTEGER_set(X509_get_serialNumber(this->x509),static_cast<long>(this->serialnum)) == 0)
    {
        this->get_ssl_errors();
        return 1;
    }

    if ((this->startDate == nullptr) || (this->endDate == nullptr))
    {
        return 2;
    }
    if ((X509_set_notBefore(this->x509,this->startDate)!=1)
            || (X509_set_notAfter(this->x509,this->endDate)!=1))
    {
            this->get_ssl_errors();
            return 1;
    }

    if (X509_set_pubkey(this->x509,this->pkey) == 0)
    {
        this->get_ssl_errors();
        return 1;
    }

    x509Name=X509_get_subject_name(this->x509);

    for (int i=0; i<this->subjectNum;i++)
    {
        if (X509_NAME_add_entry_by_txt(x509Name,this->subject_id[i],
                                   MBSTRING_ASC, this->subject[i], -1,
                                   -1, 0) == 0)
        {
            this->get_ssl_errors();
            return 1;
        }
    }
    X509_set_issuer_name(this->x509,x509Name);
    /* Add various extensions: standard extensions */
    for (i=0;i<this->extensionNum;i++)
    {
        if (this->extensionCritical[i]==0)
            value="";
        else
            value="critical,";

        value += this->extensionVal[i].data();
        if (this->add_ext_bytxt(this->x509,this->extensionName[i].data(),value.data()) == 0)
        {
            this->get_ssl_errors();
            return 1;
        }
    }
    //this->add_ext(this->x509, NID_basic_constraints, (char*)"critical,CA:TRUE");

    if (!X509_sign(this->x509,this->pkey,this->useDigest))
    {
        this->get_ssl_errors();
        return 1;
    }
    if (this->output_display != nullptr) this->output_display("finished certificate generation\n");
    return 0;
}

int SSLCertificates::sign_cert(SSLCertificates* certToSign, SSLCertificates* signingCert, unsigned int serial)
{
    X509_NAME2 *x509Name, *x509NameSigner;
    std::string value;
    int i;

    if (this->output_display != nullptr) this->output_display("Starting certificate signing\n");
    if (X509_set_version(this->x509,2) == 0)
    {
        this->get_ssl_errors();
        return 1;
    }
    if (ASN1_INTEGER_set(X509_get_serialNumber(this->x509),static_cast<long>(serial)) == 0)
    {
        this->get_ssl_errors();
        return 1;
    }

    // Add start and end date
    if ((this->startDate == nullptr) || (this->endDate == nullptr))
    {
        return 2;
    }
    if ((X509_set_notBefore(this->x509,this->startDate)!=1)
            || (X509_set_notAfter(this->x509,this->endDate)!=1))
    {
            this->get_ssl_errors();
            return 1;
    }

    // set public key from certToSign
    if (X509_set_pubkey(this->x509,X509_REQ_get0_pubkey(certToSign->csr)) == 0)
    {
        this->get_ssl_errors();
        return 1;
    }

    // set subject name from certToSign
    x509Name=X509_REQ_get_subject_name(certToSign->csr);
    if (X509_set_subject_name(this->x509,x509Name) != 1)
    {
        this->get_ssl_errors();
        return 1;
    }

    // Set issuer name
    x509NameSigner=X509_get_subject_name(signingCert->x509);
    X509_set_issuer_name(this->x509,x509NameSigner);

    /* Add various extensions: standard extensions */
    for (i=0;i<this->extensionNum;i++)
    {
        if (this->extensionCritical[i]==0)
            value="";
        else
            value="critical,";

        value += this->extensionVal[i].data();
        if (this->add_ext_bytxt(this->x509,this->extensionName[i].data(),value.data()) == 0)
        {
            this->get_ssl_errors();
            return 1;
        }
    }
    //this->add_ext(this->x509, NID_basic_constraints, (char*)"critical,CA:TRUE");

    // TODO : check if signingCert has isCA = true
    if (!X509_sign(this->x509,signingCert->pkey,this->useDigest))
    {
        this->get_ssl_errors();
        return 1;
    }
    if (this->output_display != nullptr) this->output_display("finished certificate generation\n");
    return 0;
}

int SSLCertificates::create_csr()
{
    X509_NAME2   *x509Name = nullptr;
    int i;
    std::string value;
    //if (RAND_load_file(_RAND_FILENAME, _RAND_LOADSIZE) != _RAND_LOADSIZE) {

    if (!X509_REQ_set_version(this->csr, 0L))        /* Version 1 */
    {
        this->get_ssl_errors();
        return 1;
    }

    if (!X509_REQ_set_pubkey(this->csr, this->pkey))
    {
        this->get_ssl_errors();
        return 1;
    }

    if (!(x509Name  = X509_REQ_get_subject_name(this->csr)))
    {
        this->get_ssl_errors();
        return 1;
    }

    for (int i=0; i<this->subjectNum;i++)
    {
        if (X509_NAME_add_entry_by_txt(x509Name,this->subject_id[i],
                                   MBSTRING_ASC, this->subject[i], -1,
                                   -1, 0) == 0)
        {
            this->get_ssl_errors();
            return 1;
        }
    }

    /* Add various extensions: standard extensions */
    for (i=0;i<this->extensionNum;i++)
    {
        if (this->extensionCritical[i]==0)
            value="";
        else
            value="critical,";

        value += this->extensionVal[i].data();
        if (this->add_ext_bytxt(this->csr,this->extensionName[i].data(),value.data()) == 0)
        {
            this->get_ssl_errors();
            return 1;
        }
    }
    if (!X509_REQ_sign(this->csr, this->pkey, this->useDigest)) {
        this->get_ssl_errors();
        return 1;
    }
    return 0;
}

int SSLCertificates::get_csr_PEM(char *Skey, size_t maxlength) {

    BIO *mem = BIO_new(BIO_s_mem());
    BUF_MEM *bptr;
    if (!PEM_write_bio_X509_REQ(mem,this->csr))
    {
        this->get_ssl_errors();
        BIO_free(mem);
        return 3;
    }

    BIO_get_mem_ptr(mem, &bptr);

    if (bptr->length == 0) {
        BIO_free(mem);return 1;
    }
    if (bptr->length >= maxlength) {
        BIO_free(mem);return 2;
    }
    strncpy(Skey,bptr->data,bptr->length);
    Skey[bptr->length+1]='\0';
    BIO_free(mem);
    return 0;
}

int SSLCertificates::get_csr_HUM(std::string& csrHumanReadable) {

    BIO *mem = BIO_new(BIO_s_mem());
    BUF_MEM *bptr;


    if (X509_REQ_print(mem,this->csr)==0) {
        this->get_ssl_errors();
        csrHumanReadable = "Error printing certificate";
        return 3;
    }

    BIO_get_mem_ptr(mem, &bptr);

    if (bptr->length == 0) {
        BIO_free(mem);return 1;
    }

    csrHumanReadable.reserve(bptr->length+2);
    csrHumanReadable.assign(bptr->data,bptr->length);
    csrHumanReadable[bptr->length+1]='\0';
    BIO_free(mem);
    return 0;
}

int SSLCertificates::set_csr_PEM(const char* Skey, char* password)
{
    BIO *mem = BIO_new(BIO_s_mem());
    BIO_puts(mem,Skey);

    this->csr = PEM_read_bio_X509_REQ(mem,nullptr,
                    &SSLCertificates::pem_password_callback,password);
        //(BIO *bp, EVP_PKEY **x,pem_password_cb *cb, void *u);
    BIO_free(mem);
    if (this->csr == nullptr)
    {
        this->get_ssl_errors();

      if ((ERR_GET_REASON(this->SSLErrorList[this->SSLErrorNum])==OPENSSL_BAD_PASSWORD_ERR)
          || (ERR_GET_REASON(this->SSLErrorList[this->SSLErrorNum])==OPENSSL_BAD_DECRYPT_ERR))

        {
            this->empty_ssl_errors();
            return 2;
        }
        return 1;
    }
    return 0;
}

int SSLCertificates::get_cert_PEM(std::string& Skey,X509* locX509)
{
  BIO *mem = BIO_new(BIO_s_mem());
  BUF_MEM *bptr;
  // take local if not specified
  if (locX509==nullptr) locX509=this->x509;

  if (!PEM_write_bio_X509(mem,locX509))
  {
      this->get_ssl_errors();
      BIO_free(mem);
      return 3;
  }

  BIO_get_mem_ptr(mem, &bptr);

  if (bptr->length == 0) {
      BIO_free(mem);return 1;
  }
  Skey.reserve(bptr->length+2);
  Skey.assign(bptr->data,bptr->length);
  Skey[bptr->length+1]='\0';
  BIO_free(mem);
  return 0;
}

int SSLCertificates::get_cert_HUM(std::string& cert) {

    BIO *mem = BIO_new(BIO_s_mem());
    BUF_MEM *bptr;


    if (X509_print(mem,this->x509)==0) {
        this->get_ssl_errors();
        cert = "Error printing certificate";
        return 3;
    }

    BIO_get_mem_ptr(mem, &bptr);

    if (bptr->length == 0) {
        BIO_free(mem);return 1;
    }
    cert.reserve(bptr->length+2);
    cert.assign(bptr->data,bptr->length);
    cert[bptr->length+1]='\0';
    BIO_free(mem);
    return 0;
}

int SSLCertificates::get_DN_Elmt_from_name(char *CN, size_t maxlength, X509_NAME2 *certname, int NID)
{
  int common_name_loc = -1;
   X509_NAME_ENTRY *common_name_entry = nullptr;
   ASN1_STRING *common_name_asn1 = nullptr;

   // Find the position of the field in the Subject field of the certificate
   common_name_loc = X509_NAME_get_index_by_NID(certname, NID, -1);
   if (common_name_loc < 0) {
           return 1;
   }
   // Extract the field
   common_name_entry = X509_NAME_get_entry(certname, common_name_loc);
   if (common_name_entry == nullptr) {
           return 1;
   }
   // Convert the field to a C string
   common_name_asn1 = X509_NAME_ENTRY_get_data(common_name_entry);
   if (common_name_asn1 == nullptr) {
           return 1;
   }
#if OPENSSL_VERSION_NUMBER > 0x10100000L
   const char * common_name_str = reinterpret_cast<const char*>(ASN1_STRING_get0_data(common_name_asn1));
#else
   common_name_str = (char *) ASN1_STRING_data(common_name_asn1);
#endif
   // Make sure there isn't an embedded NUL character in the CN
   if (static_cast<size_t>(ASN1_STRING_length(common_name_asn1)) != strlen(common_name_str)) {
           return 1;
   }
   if (strlen(common_name_str)>=maxlength)
     {
       return 2;
     }
   strncpy(CN,common_name_str,strlen(common_name_str));
   CN[strlen(common_name_str)]=0;
   return 0;
}

int SSLCertificates::get_CN_from_name(char* CN,size_t maxlength, X509_NAME2* certname)
{
  return this->get_DN_Elmt_from_name(CN,maxlength,certname,NID_commonName);
}

int SSLCertificates::get_cert_subject_from_name(certType certORcsr, std::string *oCN, std::string *oC, std::string *oS,
                                                std::string *oL, std::string *oO, std::string *oOU,
                                                std::string *omail)
{
  size_t maxlength=this->subjectElmntMaxSize;
  char buffer[this->subjectElmntMaxSize];
  X509_NAME2* certname;
  switch(certORcsr)
  {
    case Certificate: certname=X509_get_subject_name(this->x509);break;
    case CSR: certname=X509_REQ_get_subject_name(this->csr);break;
    case NoCert: return 1;
  }
  *omail="";
  if (this->get_DN_Elmt_from_name(buffer,maxlength,certname,CN_NID) == 0)
  {
    std::string CN(buffer);
    size_t index = CN.find("/emailAddress=");
    if (index == std::string::npos)
    { // not found
      oCN->assign(buffer);
      *omail="";
    }
    else
    {
      *oCN=CN.substr(0,index);
      *omail=CN.substr(index+14); // remove "/emailAddress"
    }
  }
  else *oCN="";
  if (this->get_DN_Elmt_from_name(buffer,maxlength,certname,C_NID) == 0)
    oC->assign(buffer);
  else *oC="";
  if (this->get_DN_Elmt_from_name(buffer,maxlength,certname,S_NID) == 0)
    oS->assign(buffer);
  else *oS="";
  if (this->get_DN_Elmt_from_name(buffer,maxlength,certname,L_NID) == 0)
    oL->assign(buffer);
  else *oL="";
  if (this->get_DN_Elmt_from_name(buffer,maxlength,certname,O_NID) == 0)
    oO->assign(buffer);
  else *oO="";
  if (this->get_DN_Elmt_from_name(buffer,maxlength,certname,OU_NID) == 0)
    oOU->assign(buffer);
  else *oOU="";
  return 0;
}

int SSLCertificates::get_cert_CN(char* CN,size_t maxlength, X509* cert)
{
   if (cert == nullptr) // TODO : check if cert is defined (checking nullptr ids not enough as the structure is defined in constructor). if not, the program crash
   {
     if (this->x509 == nullptr) return 1;
     cert=this->x509;
   }
   return this->get_CN_from_name(CN,maxlength,X509_get_subject_name(cert));
}

int SSLCertificates::get_csr_CN(char* CN,size_t maxlength, X509_REQ* csr)
{
   if (csr == nullptr) // TODO : check if cert is defined (checking nullptr ids not enough as the structure is defined in constructor). if not, the program crash
   {
     if (this->csr == nullptr) return 1;
     csr=this->csr;
   }
   return this->get_CN_from_name(CN,maxlength,X509_REQ_get_subject_name(csr));
}

int SSLCertificates::set_cert_PEM(const char* Skey, const char* password)
{
    BIO *mem = BIO_new(BIO_s_mem());
    BIO_puts(mem,Skey);

    this->x509 = PEM_read_bio_X509(mem,nullptr,
                    &SSLCertificates::pem_password_callback,const_cast<void*>(reinterpret_cast<const void*>(password)));
        //(BIO *bp, EVP_PKEY **x,pem_password_cb *cb, void *u);
    BIO_free(mem);
    if (this->x509 == nullptr)
    {
        this->get_ssl_errors();

        if ((ERR_GET_REASON(this->SSLErrorList[this->SSLErrorNum])==OPENSSL_BAD_PASSWORD_ERR)
          || (ERR_GET_REASON(this->SSLErrorList[this->SSLErrorNum])==OPENSSL_BAD_DECRYPT_ERR))

        {
            this->empty_ssl_errors();
            return 2;
        }
        return 1;
    }
    return 0;
}

int SSLCertificates::get_key_PEM(std::string * Skey, std::string password)
{
    BIO *mem = BIO_new(BIO_s_mem());
    BUF_MEM *bptr;

    switch (this->keyType)
    {
    case KeyDSA: // PEM_write_bio_DSAPrivateKey
    case KeyRSA:
    case KeyEC:
        if (password=="")
        {
          if (!PEM_write_bio_PrivateKey(mem,this->pkey,nullptr,nullptr,0,nullptr, nullptr))
          {
              this->get_ssl_errors();
              BIO_free(mem);
              return 3;
          }
        }
        else
        {
          if (PEM_write_bio_PKCS8PrivateKey(mem,this->pkey,this->useCipher,
                                            const_cast<char* >(password.c_str()),
                                            static_cast<int>(password.size()),nullptr, nullptr) == 0)
          {
              this->get_ssl_errors();
              BIO_free(mem);
              return 3;
          }
        }
        break;
    default:
        BIO_free(mem);
        return 3;
    }


    BIO_get_mem_ptr(mem, &bptr);

    if (bptr->length == 0) {
        this->get_ssl_errors();BIO_free(mem);return 1;
    }
    if (bptr->length >= MAX_IO_BUFFER_SIZE) {
        BIO_free(mem);return 2;
    }
    Skey->assign(bptr->data,bptr->length);
    BIO_free(mem);
    return 0;
}

int SSLCertificates::get_key_HUM(std::string * Skey) {

    BIO *mem = BIO_new(BIO_s_mem());
    BUF_MEM *bptr;

    switch (this->keyType) {
    case KeyRSA:
#if OPENSSL_VERSION_NUMBER > 0x10100000L
        if (RSA_print(mem,EVP_PKEY_get0_RSA(pkey),0)==0) {
#else
        if (RSA_print(mem,pkey->pkey.rsa,0)==0) {
#endif
          this->get_ssl_errors();
            *Skey="Error printing RSA key";
            return 3;
        }
        break;
    case KeyEC:
#if OPENSSL_VERSION_NUMBER > 0x10100000L
        if (EC_KEY_print(mem,EVP_PKEY_get0_EC_KEY(pkey),0)==0)
#else
        if (EC_KEY_print(mem,pkey->pkey.ec,0)==0)
#endif
        {
            this->get_ssl_errors();
            *Skey="Error printing EC key";
            return 3;
        }
        break;
    case KeyDSA:
#if OPENSSL_VERSION_NUMBER > 0x10100000L
        if (DSA_print(mem,EVP_PKEY_get0_DSA(pkey),0)==0)
#else
        if (DSA_print(mem,pkey->pkey.dsa,0)==0)
#endif
        {
            this->get_ssl_errors();
            *Skey="Error printing DSA key";
            return 3;
        }
        break;
    default:
        this->get_ssl_errors();
        *Skey="Unknown key";
        return 3;
    }

    BIO_get_mem_ptr(mem, &bptr);

    if (bptr->length == 0) {
        this->get_ssl_errors();
        BIO_free(mem);return 1;
    }
    if (bptr->length >= MAX_IO_BUFFER_SIZE) {
        this->get_ssl_errors();
        BIO_free(mem);return 2;
    }
    Skey->assign(bptr->data,bptr->length);
    BIO_free(mem);
    return 0;
}

int SSLCertificates::set_key_PEM(const char* Skey, char* password=nullptr)
{
    BIO *mem = BIO_new(BIO_s_mem());
    BIO_puts(mem,Skey);

    this->pkey = PEM_read_bio_PrivateKey(mem,nullptr,
                    &SSLCertificates::pem_password_callback,password);
    //printf("keytype : %i\n",this->pkey->type);fflush (stdout);
    BIO_free(mem);
    if (this->pkey == nullptr)
    {
        int firstError=this->SSLErrorNum+1;
        this->get_ssl_errors();

        if ((ERR_GET_REASON(this->SSLErrorList[firstError])==OPENSSL_BAD_PASSWORD_ERR)
            || (ERR_GET_REASON(this->SSLErrorList[firstError])==OPENSSL_BAD_DECRYPT_ERR))

        {
            this->empty_ssl_errors();
            return 2;
        }

        printf("SSL error : %i\n",ERR_GET_REASON(this->SSLErrorList[this->SSLErrorNum]));fflush (stdout);

        return 1;
    }
#if OPENSSL_VERSION_NUMBER > 0x10100000L
    switch (EVP_PKEY_type(EVP_PKEY_id(this->pkey)))
#else
    switch (get_key_type())
#endif
    {
    case EVP_PKEY_RSA:
        this->keyType=KeyRSA;
        break;
    case EVP_PKEY_EC:
        this->keyType=KeyEC;
        break;
    case EVP_PKEY_DSA:
        this->keyType=KeyDSA;
        break;
    default:
        return 3;
    }

    return 0;
}

int SSLCertificates::get_key_type(char* keytype,unsigned int size)
{
    switch (this->keyType)
    {
    case KeyRSA:
    case KeyDSA:
    case KeyEC:
        strcpy(keytype,keyTypeList[this->keyType-1]);
        return 0;
    }
    strcpy(keytype,"Unkown");
    return 1;
}

int SSLCertificates::get_key_params(keyTypes* keytype,std::string &keyTypeString,int* rdsa,std::string &ectype)
{
    *keytype=static_cast<keyTypes>(this->keyType);
    keyTypeString = keyTypeList[this->keyType-1];
    switch (this->keyType)
    {
      case KeyRSA:
      {
          *rdsa=0;
          RSA * rsa_key = EVP_PKEY_get1_RSA(this->pkey);
          if (rsa_key==nullptr) return 1;
          *rdsa = RSA_bits(rsa_key);
          RSA_free(rsa_key);
          return 0;
       }
      case KeyDSA:
      {
          *rdsa=0;
          DSA * dsa_key = EVP_PKEY_get1_DSA(this->pkey);
          if (dsa_key==nullptr) return 1;
          *rdsa = DSA_bits(dsa_key);
          DSA_free(dsa_key);
          return 0;
       }
      case KeyEC:
      {
          EC_KEY * ec_key=EVP_PKEY_get1_EC_KEY(this->pkey); // get EC key from private key
          const EC_GROUP * ec_group=EC_KEY_get0_group(ec_key); // get group from key
          int ec_nid=EC_GROUP_get_curve_name(ec_group);
          ectype="unknown";
          for (int i=0;i<this->keyECListNum-1;i++)
          {
             if (ec_nid==this->keyECListNIDCode[i])
             {
               ectype = this->keyECList[i];
             }
          }
      }

          return 0;
    }
    keyTypeString = "Unkown";
    return 1;
}

/**
 * @brief SSLCertificates::save_to_pkcs12
 * @param file FILE Handler to file
 * @param name char* Friendly name of p12
 * @param pass char* password
 * @param keyEcrypt int Key encryption NID or 0 for default
 * @param certEcrypt int Certificate encryption NID or 0 for default
 * @param keyIterations int Number of key iterations or 0 for default (PKCS12_DEFAULT_ITER)
 * @param macIterations int Number of MAC iterations or 0 for default (1)
 * @parma keyType int 0 = none, KEY_SIG = Signature key, KEY_EX = Export key
 * @return int 1=error generating key, 2=error saving file, 0=OK
 */
int SSLCertificates::save_to_pkcs12(FILE *file, char* name,char* pass,
                       int keyEcrypt, int certEcrypt, int  keyIterations, int macIterations, int keyType )
{
    PKCS12 *newkey;
    if (keyIterations == 0 ) keyIterations = PKCS12_DEFAULT_ITER;
    switch (keyType)
    {
      case 0: break;
      case KEY_SIG : break;
      case KEY_EX : break;
      default:
        // Error message TODO
        return 1;
        break;
    }

    newkey = PKCS12_create(
                pass, //char *pass
                name, // char *name
                this->pkey, //EVP_PKEY *pkey
                this->x509, //X509 *cert,
                this->ca,   //STACK_OF(X509) *ca,
                keyEcrypt,      //    int nid_key
                certEcrypt,      //    int nid_cert
                keyIterations,      //    int iter
                macIterations,      //    int mac_iter
                keyType); //   int keytype
    if (newkey==nullptr)
      {
        this->get_ssl_errors();
        return 1;
      }
    if (i2d_PKCS12_fp(file,newkey) <=0)
    {
        this->get_ssl_errors();
        return 2;
    }
    PKCS12_free(newkey);
    return 0;
}

char *SSLCertificates::find_friendly_name(PKCS12 *p12)
{
    STACK_OF(PKCS7) *safes = PKCS12_unpack_authsafes(p12);
    int n, m;
    char *name = nullptr;
    PKCS7 *safe;
    STACK_OF(PKCS12_SAFEBAG) *bags;
    PKCS12_SAFEBAG *bag;

    if ((safes = PKCS12_unpack_authsafes(p12)) == nullptr)
        return nullptr;

    for (n = 0; n < sk_PKCS7_num(safes) && name == nullptr; n++) {
        safe = sk_PKCS7_value(safes, n);
        if (OBJ_obj2nid(safe->type) != NID_pkcs7_data
                || (bags = PKCS12_unpack_p7data(safe)) == nullptr)
            continue;

        for (m = 0; m < sk_PKCS12_SAFEBAG_num(bags) && name == nullptr; m++) {
            bag = sk_PKCS12_SAFEBAG_value(bags, m);
            name = PKCS12_get_friendlyname(bag);
        }
        sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
    }

    sk_PKCS7_pop_free(safes, PKCS7_free);

    return name;
}

int SSLCertificates::get_pkcs12_certs()
{
  // not initialized or empty
  if ((ca==nullptr)||(sk_X509_num(ca)==0)) return 0;

  unsigned int num=static_cast<unsigned int>(sk_X509_num(ca));
  char p12CN[100];
  std::string p12CNStr;
  X509* p12Cert;

  caListCN.reserve(num);
  caListCerts.reserve(num);
  for (unsigned int i=0;i<num;i++)
  {
    p12Cert=X509_dup(sk_X509_value(ca,static_cast<int>(i)));
    caListCerts.push_back(p12Cert);
    if (this->get_cert_CN(p12CN,100,p12Cert) == 0)
    {
      p12CNStr.assign(p12CN);      
    }
    else
    {
       p12CNStr.assign("<Unknown CN>");
    }
    caListCN.push_back(p12CNStr);
  }
  return static_cast<int>(num);
}

int SSLCertificates::get_pkcs12_certs_num()
{
  return  static_cast<int>(this->caListCerts.size());
}

std::string SSLCertificates::get_pkcs12_certs_CN(unsigned int n)
{
  if (n>=this->caListCN.size()) return std::string("");
  return this->caListCN.at(n);
}

int SSLCertificates::get_pkcs12_certs_pem(unsigned int n,char *Skey, size_t maxlength)
{
  if (n>=this->caListCerts.size()) return 10;
  std::string certPEM;
  int retcode = get_cert_PEM(certPEM,this->caListCerts.at(n));
  strcpy(Skey,certPEM.c_str());
  return retcode;
}

int SSLCertificates::load_pkcs12(FILE *file, const char* password)
{
  PKCS12 *p12;
  if ((p12=d2i_PKCS12_fp(file,nullptr)) == nullptr)
  {
    this->get_ssl_errors();
    return 1;
  }
  if (!PKCS12_parse(p12, password, &pkey, &x509, &ca)) {
      this->get_ssl_errors();
      return 2;
  }
  switch (EVP_PKEY_type(EVP_PKEY_id(this->pkey)))
  {
    case EVP_PKEY_RSA: this->keyType = KeyRSA; break;
    case EVP_PKEY_DSA: this->keyType = KeyDSA; break;
    case EVP_PKEY_EC: this->keyType = KeyEC; break;
    default: PKCS12_free(p12);return 3;
  }
  char * name=find_friendly_name(p12);
  get_pkcs12_certs();
  p12Name=strdup(name);
  // TODO check mem leak. free(name) doest sigsev.
  //free(name);
  PKCS12_free(p12);
  return 0;
}

char *SSLCertificates::get_pkcs12_name()
{
  return this->p12Name;
}

int SSLCertificates::add_pkcs12_ca(const char* Skey)
{
  if (ca==nullptr)
  {
    ca = sk_X509_new_null();
  }
  SSLCertificates * loccert=new SSLCertificates();
  if (loccert->set_cert_PEM(Skey,"") != 0)
  {
    return 1;
  }
  sk_X509_push(ca,loccert->x509);
  return 0;
}

int SSLCertificates::callback(int p, int , BN_GENCB*)
{
    char c[2];
    c[0]='B';
    c[1]=0;

    if (p == 0) c[0]='.';
    if (p == 1) c[0]='+';
    if (p == 2) c[0]='*';
    if (p == 3) c[0]='\n';
    SSLCertificates::output_display(c);
    //printf("callback : %i \n",p);
    if (SSLCertificates::abortnow == 1) return 0;
    return 1;
}

int SSLCertificates::pem_password_callback (char *buf, int size, int /*rwflag*/, void *userdata)
{
    if (userdata==nullptr)
    {
        buf[0]='\0';
        return 0;
        //printf("nopass\n");
        //fflush(stdout);
    }
    unsigned int size_t=static_cast<unsigned int>(size);
    //printf("Pass :%s / sizebuf : %i / %i\n",(char*)userdata,size,strlen((char*)userdata));
    //fflush(stdout);
    if (size_t < strlen(static_cast<char*>(userdata)))
        strncpy(buf,static_cast<char*>(userdata), size_t);
    else
        strncpy(buf, static_cast<char*>(userdata),strlen(static_cast<char*>(userdata)));
    buf[strlen(static_cast<char*>(userdata))] = '\0';
    //printf("Pass :%s / %i\n",buf,strlen(buf));
    //fflush(stdout);
    return(static_cast<int>(strlen(buf)));
}

int SSLCertificates::x509_extension_add(std::string extensionNameI, std::string extensionValI, int extensionCriticalI)
{
    std::string value = (extensionCriticalI == 1)?"critical,":"";
    value += extensionValI;
    X509 *test=X509_new();
    if (test==nullptr) return 2;

    if (this->add_ext_bytxt(test,extensionNameI.data(),value.data()) == 0)
    {
        this->get_ssl_errors();
        X509_free(test);
        return 1;
    }
    this->extensionName[this->extensionNum]=extensionNameI;
    this->extensionVal[this->extensionNum]=extensionValI;
    this->extensionCritical[this->extensionNum++]=extensionCriticalI;
    return 0;
}

int SSLCertificates::x509_extension_get(std::vector<SSLCertificates::x509Extension> *extensions)
{
  X509_EXTENSION * ext;
  int extNID;
  int numExt=X509_get_ext_count(this->x509);
  for (int i=0;i<numExt;i++)
  {
    x509Extension extVal = {"","",0,"",false};
    ext=X509_get_ext(this->x509,i);

    if (ext==nullptr) continue;

    extVal.critical = (X509_EXTENSION_get_critical(ext)==1) ? true : false;
    //ASN1_OCTET_STRING * extValue=X509_EXTENSION_get_data(ext);
    extNID=OBJ_obj2nid(X509_EXTENSION_get_object(ext));
    extVal.NID=extNID;
    switch (extNID)
    {
      case NID_subject_alt_name:
      {
        GENERAL_NAMES *names = nullptr;
        names = (GENERAL_NAMES *) X509V3_EXT_d2i(ext);
        if (names == nullptr) continue;
        int numAltNames=sk_GENERAL_NAME_num(names);
        for (int i=0;i<numAltNames;i++)
        {
           GENERAL_NAME *currentName = sk_GENERAL_NAME_value(names, i);
           const char *AltName;
           std::string comma=(extVal.values.empty())?"":", ";
           switch (currentName->type)
           {
             case GEN_DNS:
               AltName = (const char *) ASN1_STRING_get0_data(currentName->d.dNSName);
               extVal.values += comma + "DNS:" + std::string(AltName);
             break;
             case GEN_URI:
               AltName = (const char *) ASN1_STRING_get0_data(currentName->d.uniformResourceIdentifier);
               extVal.values += comma + "URI:" + std::string(AltName);
             break;
           }
        }
        extVal.name=SN_subject_alt_name;
        if ( ! extVal.values.empty() )
        {
          extensions->push_back(extVal);
        }
        sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free); // Free stack
      }
      break;
      case NID_basic_constraints:
      {
          BASIC_CONSTRAINTS *bs = (BASIC_CONSTRAINTS *) X509V3_EXT_d2i(ext);
          if (bs == nullptr) continue;
          extVal.values = (bs->ca == 255) ? "CA:TRUE" : "CA:FALSE";
          long pathlen = ASN1_INTEGER_get(bs->pathlen);
          if (pathlen != 0)
          {
            extVal.values += ",pathlen:" + std::to_string(pathlen);
          }
          extVal.name=SN_basic_constraints;
          extensions->push_back(extVal);

          BASIC_CONSTRAINTS_free(bs);
      }
      break;
      case NID_key_usage:
      {
          ASN1_BIT_STRING *usage = (ASN1_BIT_STRING *)X509V3_EXT_d2i(ext);
          if (usage && (usage->length > 0))
          {
              int octet=0,bit=128;
              for (int i=0;i<this->x509KeyUsageListNum;i++)
              {
                 if ((usage->data[octet] & bit) == bit)
                 {
                   extVal.values += (extVal.values.empty())?"":", ";
                   extVal.values += this->x509KeyUsageList[i];
                 }
                 if (bit==1)
                 {
                   bit=128;
                   octet++;
                   if (usage->length <= octet) // if next bit is not used
                     break;
                 }
                 else
                   bit/=2;
              }
          }
          //extVal.values += "/l="+std::to_string(usage->length)+ "/" + std::to_string(usage->data[0]) + "/" + std::to_string(usage->data[1]);
          extVal.name=SN_key_usage;
          if ( ! extVal.values.empty() )
          {
            extensions->push_back(extVal);
          }
      }
      break;
      case NID_crl_distribution_points:
      {  // TODO : missing paramters "reasons". Only gets URI
          CRL_DIST_POINTS * crlDistPoints = nullptr;
          crlDistPoints = (CRL_DIST_POINTS *) X509V3_EXT_d2i(ext);
          if (crlDistPoints == nullptr) continue;
          int numCrlDistPoints=sk_DIST_POINT_num(crlDistPoints);
          for (int i=0;i<numCrlDistPoints;i++) // Parse each distribution points
          {
             DIST_POINT *distPoint = sk_DIST_POINT_value(crlDistPoints, i);
             const char* distPointName;
             std::string comma=(extVal.values.empty())?"":", ";

             if (distPoint->distpoint == nullptr || distPoint->distpoint->type != 0) continue;  // No distribution point or bad type

             for (int j=0;j<sk_GENERAL_NAME_num(distPoint->distpoint->name.fullname);j++)
             {
                GENERAL_NAME *crlDpName =   sk_GENERAL_NAME_value(distPoint->distpoint->name.fullname,j);
                if (crlDpName->type == GEN_URI)
                {
                  distPointName = (const char*) ASN1_STRING_get0_data(crlDpName->d.uniformResourceIdentifier);
                  extVal.values += comma + std::string(distPointName);
                  break;
                }
             }

          }
          extVal.name=SN_crl_distribution_points;
          if ( ! extVal.values.empty() )
          {
            extensions->push_back(extVal);
          }
          sk_DIST_POINT_free(crlDistPoints); // Free stack
      }
      break;
      case NID_ext_key_usage:
      {
          EXTENDED_KEY_USAGE * usage = (EXTENDED_KEY_USAGE *) X509V3_EXT_d2i(ext);
          for (int i = 0; i < sk_ASN1_OBJECT_num(usage); i++)
          {
              int usageNID = OBJ_obj2nid(sk_ASN1_OBJECT_value(usage, i));
              try
              {
                std::string comma=(extVal.values.empty())?"":", ";
                extVal.values += comma + this->x509ExtKeyUsageList.at(usageNID);
              }
              catch (const std::out_of_range& oor)
              { // Unknown NID
                 // TODO : put a warning of some type
              }
          }
          sk_ASN1_OBJECT_pop_free(usage, ASN1_OBJECT_free);
          extVal.name = SN_ext_key_usage;
          if ( ! extVal.values.empty() )
          {
            extensions->push_back(extVal);
          }
      }
      break;
      default:
      break;
    }

    //BASIC_CONSTRAINTS* test;
    //AUTHORITY_KEYID *test2;
    // TODO : get extensions and put it in the GUI. Seems every extension type has to be get by specific methods....
    // END TODO
  }
  return 0;
}

int SSLCertificates::add_ext(X509 *cert, int nid, const char *value)
{
    X509_EXTENSION *ex;
    X509V3_CTX ctx;
    /* This sets the 'context' of the extensions. */
    /* No configuration database */
    X509V3_set_ctx_nodb(&ctx);
    /* Issuer and subject certs: both the target since it is self signed,
     * no request and no CRL
     */
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
    ex = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
    if (!ex)
        return 0;

    X509_add_ext(cert,ex,-1);
    X509_EXTENSION_free(ex);
    return 1;
}

int SSLCertificates::add_ext_bytxt(X509 *cert, const char* nid, const char *value)
{
    X509_EXTENSION *ex;
    X509V3_CTX ctx;
    /* This sets the 'context' of the extensions. */
    /* No configuration database */
    X509V3_set_ctx_nodb(&ctx);
    /* Issuer and subject certs: both the target since it is self signed,
     * no request and no CRL
     */
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
    ex = X509V3_EXT_conf(nullptr, &ctx, nid, value);
    if (!ex)
        return 0;

    X509_add_ext(cert,ex,-1);
    X509_EXTENSION_free(ex);
    return 1;
}

int SSLCertificates::add_ext_bytxt(X509_REQ *csr, const char* nid, const char *value)
{
    X509_EXTENSION *ex;
    X509V3_CTX ctx;

    // Create a stack for extensions
    STACK_OF(X509_EXTENSION) *extensions = sk_X509_EXTENSION_new_null();

    X509V3_set_ctx(&ctx, nullptr, nullptr, nullptr, nullptr, 0);
    ex = X509V3_EXT_conf(nullptr, &ctx, nid, value);

    if (!ex) {
        printf(" failed to create the extension");
        return 0;
    }

    if (sk_X509_EXTENSION_push(extensions, ex)) {
    } else {
        printf( "Error adding X509 extension to stack\n");
    }

    if (!X509_REQ_add_extensions(csr,extensions))
        printf("Error, could not add extension");

    X509_EXTENSION_free(ex);

    sk_X509_EXTENSION_free(extensions);
    return 1;
}

int SSLCertificates::get_key_type()
{
    if (this->pkey == nullptr)
        return NID_undef;

#if OPENSSL_VERSION_NUMBER > 0x10100000L
    switch (EVP_PKEY_type(EVP_PKEY_id(this->pkey)))
#else
    switch (EVP_PKEY_type(this->pkey->type))
#endif
    {
    case EVP_PKEY_RSA:
        return KeyRSA;
    case EVP_PKEY_DSA:
        return KeyDSA;
    case EVP_PKEY_EC:
        return KeyEC;
    case EVP_PKEY_DH:
        return NID_undef; // TODO : not implemented DH
    }
    return NID_undef;
}

int SSLCertificates::check_key() // TODO DSA and DH
{
    int retcode;
    RSA* rsa;
    DSA* dsa;
    EC_KEY* ec;
    int type=this->get_key_type();
    if (type==NID_undef) return 1;
    switch (type)
    {
    case KeyRSA :
        rsa = EVP_PKEY_get1_RSA(pkey);
        retcode = RSA_check_key(rsa);
        RSA_free(rsa);
        if (retcode ==1) return 0;
        return 1;
    case KeyDSA :
        dsa = EVP_PKEY_get1_DSA(pkey);
        //retcode = DSA_check_key(dsa); TODO : find check key function
        retcode = 0; // Send OK for now
        DSA_free(dsa);
        return retcode;
    /*case EVP_PKEY_DH :    // TODO implement DH
        DH* dh = EVP_PKEY_get1_DH(pkey);
        retcode = DH_check_key(dh);
        DH_free(dh);
        return retcode;*/
    case KeyEC :
        ec = EVP_PKEY_get1_EC_KEY(pkey);
        retcode = EC_KEY_check_key(ec);
        EC_KEY_free(ec);
        if (retcode ==1) return 0;
        return 1;
    default:
        break;

    }
    return 10;
}

int SSLCertificates::check_key_csr_match()
{
    if (this->check_key() != 0)
        return 2;

    int retcode = X509_REQ_verify(this->csr,this->pkey);
    if (retcode == 1)
    {
      return 0;
    }
    return 1;
}

int SSLCertificates::check_key_cert_match()
{
    if (this->check_key() != 0)
        return 2;
    // init context
    //SSL_CTX* ctx = SSL_CTX_new( TLSv1_2_client_method()); //  TLSv1_client_method());
    SSL_CTX* ctx = SSL_CTX_new( TLS_client_method()); //  TLSv1_client_method());
    if (ctx==nullptr)
      {
        this->get_ssl_errors();
        return 2;
      }
    // add certificate and key to context
    if (SSL_CTX_use_certificate(ctx, this->x509)!=1)
    {
        this->get_ssl_errors();
        SSL_CTX_free(ctx);
        return 2;
    }
    if (SSL_CTX_use_PrivateKey(ctx, this->pkey)!=1)
    {
        this->get_ssl_errors();
        SSL_CTX_free(ctx);
        return 2;
    }

    if (SSL_CTX_check_private_key(ctx)!=1)
    //int retcode = X509_verify(this->x509, this->pkey);
    {
        this->get_ssl_errors();
        SSL_CTX_free(ctx);
        return 1;
    }
    SSL_CTX_free(ctx);
    return 0;
}
