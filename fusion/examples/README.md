# FOQN pipeline model

`FOQN_E06_0010_Pipeline_Model.comp` is the single end-to-end reference:

`Loader_FOQN_E06_0010_Plate` → `G_InputPrep_FOQN_E06_0010` → the WIP and
clean `G_OutputPackager` branches → their respective Savers.

It uses the real 1920×1080 `_FOQN:` E06/0010 H264 plate and resolves the
corresponding FOQN WIP and clean output paths through `G_ShotConfig`. It has
no intermediate configuration groups: InputPrep and OutputPackager own their
respective processing controls.
