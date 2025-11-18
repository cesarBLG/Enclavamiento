#pragma once
enum struct ElementoRemota
{
    SIG=1,
    IC,
    PV,
    CV,
    CVA,
    CVX,
    SDT,
    AG,
    BOU,
    CH,
    CAN,
    IMV,
    FMV,
    BLQ,
    ML,
    ENE,
    DEP,
    PN,
    CCF,
    DCO,
    FRA=23,
    IGA,
    IDE,
};
struct RemotaSIG
{
    unsigned int SIG_DAT:1;
    unsigned int SIG_TIPO:2;
    unsigned int SIG_EAR:1;
    unsigned int SIG_IND:4;
    unsigned int SIG_FOCO_R:2;
    unsigned int SIG_FOCO_BL_C:2;
    unsigned int SIG_FOCO_BL_V:2;
    unsigned int SIG_FOCO_BL_H:2;
    unsigned int SIG_FOCO_AZ:2;
    unsigned int SIG_FOCO_AM:2;
    unsigned int SIG_FOCO_V:2;
    unsigned int SIG_ME:1;
    unsigned int SIG_B:1;
    unsigned int SIG_UIC:3;
    unsigned int SIG_SA:1;
    unsigned int SIG_FAI:1;
    unsigned int SIG_FAI_IND:2;
    unsigned int SIG_GRP_ARS:2;
};
struct RemotaPV
{
    unsigned int PV_DAT:1;
    unsigned int PV_IND:2;
    unsigned int PV_CS_IND:1;
};
struct RemotaCV
{
    unsigned int CV_DAT:1;
    unsigned int CV_ME:1;
    unsigned int CV_BV:1;
    unsigned int CV_OCUP_TIPO:1;
    unsigned int CV_EST:2;
    unsigned int CV_DES:2;
    unsigned int CV_CEJES_AV:1;
    unsigned int CV_CEJES_PREN:1;
    unsigned int CV_UC:3;
    unsigned int CV_NSEC:1;
};
struct RemotaIMV
{
    unsigned int IMV_DAT:1;
    unsigned int IMV_EST:3;
    unsigned int IMV_DIF_VAL:4;
};
struct RemotaFMV
{
    unsigned int FMV_DAT:1;
    unsigned int FMV_EST:3;
    unsigned int FMV_DIF_VAL:4;
    unsigned int FMV_DIF_ELEM:2;
    unsigned int FMV_ME:1;
    unsigned int FMV_BD:1;
};
struct RemotaBLQ
{
    unsigned int BLQ_DAT:1;
    unsigned int BLQ_PROHI_SAL:1;
    unsigned int BLQ_EST_SAL:3;
    unsigned int BLQ_EST_ENT:3;
    unsigned int BLQ_PROHI_ENT:1;
    unsigned int BLQ_COL_MAN:1;
    unsigned int BLQ_ACTC:2;
    unsigned int BLQ_CSB:2;
    unsigned int BLQ_NB:1;
    unsigned int BLQ_ME:1;
    unsigned int BLQ_PROX:1;
};
struct RemotaML
{
    unsigned int ML_DAT:1;
    unsigned int ML_EST:2;
    unsigned int ML_ME:1;
    unsigned int ML_ANUL_EMERG:1;
    unsigned int ML_DIF_VAL:2;
};
struct RemotaDEP
{
    unsigned int DEP_DAT:1;
    unsigned int DEP_MAN_EST:2;
    unsigned int DEP_ME:1;
    unsigned int DEP_MAN_P:2;
    unsigned int DEP_MAN_C:2;
    unsigned int DEP_CON:1;
    unsigned int DEP_BCK:1;
};
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json& j, const RemotaSIG& p);
void from_json(const json& j, RemotaSIG& p);

void to_json(json& j, const RemotaPV& p);
void from_json(const json& j, RemotaPV& p);

void to_json(json& j, const RemotaCV& p);
void from_json(const json& j, RemotaCV& p);

void to_json(json& j, const RemotaIMV& p);
void from_json(const json& j, RemotaIMV& p);

void to_json(json& j, const RemotaFMV& p);
void from_json(const json& j, RemotaFMV& p);

void to_json(json& j, const RemotaBLQ& p);
void from_json(const json& j, RemotaBLQ& p);

void to_json(json& j, const RemotaML& p);
void from_json(const json& j, RemotaML& p);

void to_json(json& j, const RemotaDEP& p);
void from_json(const json& j, RemotaDEP& p);
#endif
