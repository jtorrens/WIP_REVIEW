-- Curated labels shown before host-discovered additions. IDs are the exact
-- FuID literals exposed by Fusion 21's native ColorSpaceTransform tool.
return {
    schemaVersion = 1,
    colorSpaces = {
        { id = "REC709_COLORSPACE", label = "Rec.709" },
        { id = "REC2020_COLORSPACE", label = "Rec.2020" },
        { id = "REC2100_COLORSPACE", label = "Rec.2100" },
        { id = "DWG_COLORSPACE", label = "DaVinci Wide Gamut" },
        { id = "ACES_COLORSPACE", label = "ACES AP0" },
        { id = "ACES_AP1_COLORSPACE", label = "ACEScg / AP1" },
        { id = "P3D65_COLORSPACE", label = "P3-D65" },
        { id = "SRGB_COLORSPACE", label = "sRGB" },
    },
    gammas = {
        { id = "LINEAR_GAMMA", label = "Linear" },
        { id = "REC709_GAMMA", label = "Rec.709" },
        { id = "TWOPOINTTWO_GAMMA", label = "Gamma 2.2" },
        { id = "TWOPOINTFOUR_GAMMA", label = "Gamma 2.4" },
        { id = "DAV_INTER_OETF_GAMMA", label = "DaVinci Intermediate" },
        { id = "SRGB_GAMMA", label = "sRGB" },
        { id = "LOGC4_EI800_GAMMA", label = "ARRI LogC4" },
        { id = "SONY_SLOG3_GAMMA", label = "Sony S-Log3" },
        { id = "REC2100_PQ_OETF_GAMMA", label = "Rec.2100 PQ" },
        { id = "REC2100_HLG_OETF_GAMMA", label = "Rec.2100 HLG" },
    },
}
