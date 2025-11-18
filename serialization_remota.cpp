#include <estado_remota.h>

// ---------------- RemotaSIG ----------------

void to_json(json& j, const RemotaSIG& p)
{
    j = json{
        {"SIG_DAT", p.SIG_DAT},
        {"SIG_TIPO", p.SIG_TIPO},
        {"SIG_EAR", p.SIG_EAR},
        {"SIG_IND", p.SIG_IND},
        {"SIG_FOCO_R", p.SIG_FOCO_R},
        {"SIG_FOCO_BL_C", p.SIG_FOCO_BL_C},
        {"SIG_FOCO_BL_V", p.SIG_FOCO_BL_V},
        {"SIG_FOCO_BL_H", p.SIG_FOCO_BL_H},
        {"SIG_FOCO_AZ", p.SIG_FOCO_AZ},
        {"SIG_FOCO_AM", p.SIG_FOCO_AM},
        {"SIG_FOCO_V", p.SIG_FOCO_V},
        {"SIG_ME", p.SIG_ME},
        {"SIG_B", p.SIG_B},
        {"SIG_UIC", p.SIG_UIC},
        {"SIG_SA", p.SIG_SA},
        {"SIG_FAI", p.SIG_FAI},
        {"SIG_FAI_IND", p.SIG_FAI_IND},
        {"SIG_GRP_ARS", p.SIG_GRP_ARS}
    };
}

void from_json(const json& j, RemotaSIG& p)
{
    p.SIG_DAT      = j.value("SIG_DAT", 0);
    p.SIG_TIPO     = j.value("SIG_TIPO", 0);
    p.SIG_EAR      = j.value("SIG_EAR", 0);
    p.SIG_IND      = j.value("SIG_IND", 0);
    p.SIG_FOCO_R   = j.value("SIG_FOCO_R", 0);
    p.SIG_FOCO_BL_C= j.value("SIG_FOCO_BL_C", 0);
    p.SIG_FOCO_BL_V= j.value("SIG_FOCO_BL_V", 0);
    p.SIG_FOCO_BL_H= j.value("SIG_FOCO_BL_H", 0);
    p.SIG_FOCO_AZ  = j.value("SIG_FOCO_AZ", 0);
    p.SIG_FOCO_AM  = j.value("SIG_FOCO_AM", 0);
    p.SIG_FOCO_V   = j.value("SIG_FOCO_V", 0);
    p.SIG_ME       = j.value("SIG_ME", 0);
    p.SIG_B        = j.value("SIG_B", 0);
    p.SIG_UIC      = j.value("SIG_UIC", 0);
    p.SIG_SA       = j.value("SIG_SA", 0);
    p.SIG_FAI      = j.value("SIG_FAI", 0);
    p.SIG_FAI_IND  = j.value("SIG_FAI_IND", 0);
    p.SIG_GRP_ARS  = j.value("SIG_GRP_ARS", 0);
}

// ---------------- RemotaPV ----------------

void to_json(json& j, const RemotaPV& p)
{
    j = json{
        {"PV_DAT", p.PV_DAT},
        {"PV_IND", p.PV_IND},
        {"PV_CS_IND", p.PV_CS_IND}
    };
}

void from_json(const json& j, RemotaPV& p)
{
    p.PV_DAT   = j.value("PV_DAT", 0);
    p.PV_IND   = j.value("PV_IND", 0);
    p.PV_CS_IND= j.value("PV_CS_IND", 0);
}

// ---------------- RemotaCV ----------------

void to_json(json& j, const RemotaCV& p)
{
    j = json{
        {"CV_DAT", p.CV_DAT},
        {"CV_ME", p.CV_ME},
        {"CV_BV", p.CV_BV},
        {"CV_OCUP_TIPO", p.CV_OCUP_TIPO},
        {"CV_EST", p.CV_EST},
        {"CV_DES", p.CV_DES},
        {"CV_CEJES_AV", p.CV_CEJES_AV},
        {"CV_CEJES_PREN", p.CV_CEJES_PREN},
        {"CV_UC", p.CV_UC},
        {"CV_NSEC", p.CV_NSEC}
    };
}

void from_json(const json& j, RemotaCV& p)
{
    p.CV_DAT        = j.value("CV_DAT", 0);
    p.CV_ME         = j.value("CV_ME", 0);
    p.CV_BV         = j.value("CV_BV", 0);
    p.CV_OCUP_TIPO  = j.value("CV_OCUP_TIPO", 0);
    p.CV_EST        = j.value("CV_EST", 0);
    p.CV_DES        = j.value("CV_DES", 0);
    p.CV_CEJES_AV   = j.value("CV_CEJES_AV", 0);
    p.CV_CEJES_PREN = j.value("CV_CEJES_PREN", 0);
    p.CV_UC         = j.value("CV_UC", 0);
    p.CV_NSEC       = j.value("CV_NSEC", 0);
}

// ---------------- RemotaIMV ----------------

void to_json(json& j, const RemotaIMV& p)
{
    j = json{
        {"IMV_DAT", p.IMV_DAT},
        {"IMV_EST", p.IMV_EST},
        {"IMV_DIF_VAL", p.IMV_DIF_VAL}
    };
}

void from_json(const json& j, RemotaIMV& p)
{
    p.IMV_DAT     = j.value("IMV_DAT", 0);
    p.IMV_EST     = j.value("IMV_EST", 0);
    p.IMV_DIF_VAL = j.value("IMV_DIF_VAL", 0);
}

// ---------------- RemotaFMV ----------------

void to_json(json& j, const RemotaFMV& p)
{
    j = json{
        {"FMV_DAT", p.FMV_DAT},
        {"FMV_EST", p.FMV_EST},
        {"FMV_DIF_VAL", p.FMV_DIF_VAL},
        {"FMV_DIF_ELEM", p.FMV_DIF_ELEM},
        {"FMV_ME", p.FMV_ME},
        {"FMV_BD", p.FMV_BD}
    };
}

void from_json(const json& j, RemotaFMV& p)
{
    p.FMV_DAT      = j.value("FMV_DAT", 0);
    p.FMV_EST      = j.value("FMV_EST", 0);
    p.FMV_DIF_VAL  = j.value("FMV_DIF_VAL", 0);
    p.FMV_DIF_ELEM = j.value("FMV_DIF_ELEM", 0);
    p.FMV_ME       = j.value("FMV_ME", 0);
    p.FMV_BD       = j.value("FMV_BD", 0);
}

// ---------------- RemotaBLQ ----------------

void to_json(json& j, const RemotaBLQ& p)
{
    j = json{
        {"BLQ_DAT", p.BLQ_DAT},
        {"BLQ_PROHI_SAL", p.BLQ_PROHI_SAL},
        {"BLQ_EST_SAL", p.BLQ_EST_SAL},
        {"BLQ_EST_ENT", p.BLQ_EST_ENT},
        {"BLQ_PROHI_ENT", p.BLQ_PROHI_ENT},
        {"BLQ_COL_MAN", p.BLQ_COL_MAN},
        {"BLQ_ACTC", p.BLQ_ACTC},
        {"BLQ_CSB", p.BLQ_CSB},
        {"BLQ_NB", p.BLQ_NB},
        {"BLQ_ME", p.BLQ_ME},
        {"BLQ_PROX", p.BLQ_PROX}
    };
}

void from_json(const json& j, RemotaBLQ& p)
{
    p.BLQ_DAT       = j.value("BLQ_DAT", 0);
    p.BLQ_PROHI_SAL = j.value("BLQ_PROHI_SAL", 0);
    p.BLQ_EST_SAL   = j.value("BLQ_EST_SAL", 0);
    p.BLQ_EST_ENT   = j.value("BLQ_EST_ENT", 0);
    p.BLQ_PROHI_ENT = j.value("BLQ_PROHI_ENT", 0);
    p.BLQ_COL_MAN   = j.value("BLQ_COL_MAN", 0);
    p.BLQ_ACTC      = j.value("BLQ_ACTC", 0);
    p.BLQ_CSB       = j.value("BLQ_CSB", 0);
    p.BLQ_NB        = j.value("BLQ_NB", 0);
    p.BLQ_ME        = j.value("BLQ_ME", 0);
    p.BLQ_PROX      = j.value("BLQ_PROX", 0);
}

// ---------------- RemotaML ----------------

void to_json(nlohmann::json& j, const RemotaML& r) {
    j = nlohmann::json{
        {"ML_DAT", r.ML_DAT},
        {"ML_EST", r.ML_EST},
        {"ML_ME", r.ML_ME},
        {"ML_ANUL_EMERG", r.ML_ANUL_EMERG},
        {"ML_DIF_VAL", r.ML_DIF_VAL}
    };
}

void from_json(const nlohmann::json& j, RemotaML& r) {
    r.ML_DAT = j.value("ML_DAT", 0);
    r.ML_EST = j.value("ML_EST", 0);
    r.ML_ME = j.value("ML_ME", 0);
    r.ML_ANUL_EMERG = j.value("ML_ANUL_EMERG", 0);
    r.ML_DIF_VAL = j.value("ML_DIF_VAL", 0);
}

// ---------------- RemotaDEP ----------------

void to_json(nlohmann::json& j, const RemotaDEP& p)
{
    j = nlohmann::json{
        {"DEP_DAT", p.DEP_DAT},
        {"DEP_MAN_EST", p.DEP_MAN_EST},
        {"DEP_ME", p.DEP_ME},
        {"DEP_MAN_P", p.DEP_MAN_P},
        {"DEP_MAN_C", p.DEP_MAN_C},
        {"DEP_CON", p.DEP_CON},
        {"DEP_BCK", p.DEP_BCK}
    };
}

void from_json(const nlohmann::json& j, RemotaDEP& p)
{
    p.DEP_DAT     = j.value("DEP_DAT", 0u);
    p.DEP_MAN_EST = j.value("DEP_MAN_EST", 0u);
    p.DEP_ME      = j.value("DEP_ME", 0u);
    p.DEP_MAN_P   = j.value("DEP_MAN_P", 0u);
    p.DEP_MAN_C   = j.value("DEP_MAN_C", 0u);
    p.DEP_CON     = j.value("DEP_CON", 0u);
    p.DEP_BCK     = j.value("DEP_BCK", 0u);
}
