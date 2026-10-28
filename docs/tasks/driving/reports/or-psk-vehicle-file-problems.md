# Vehicle file problems: OR_PSK

Every ENG/WAG file under `TRAINS/TRAINSET` (4350) as TSRE reads it (task driving 01), and the 1128 include files no vehicle reads. Made 2026-10-28 by `TSRE_VEHICLE_REPORT=<file> TSRE5vc --test --test-suite vehicle-file-corpus --game-root <game folder>`: run it again after fixing.

Paths are from the game folder; a line 0 is the file as a whole. *Vehicles* counts the vehicles reading the file (*none*: an include file no vehicle reads).

## To fix (608)

Data is lost or misread here, in TSRE and mostly in Open Rails too.

### A ')' that closes no block, skipped (125)

| File | Line | Detail | Vehicles |
| --- | --- | --- | --- |
| `TRAINS/TRAINSET/B-BLG_Laaers700_BTS/BLG_Laaers_B_Yaris.wag` | 67 |  | 1 |
| `TRAINS/TRAINSET/CDC_Rils_minipack/CDC_31_54_3541_022-2_Rils_rako.wag` | 64 |  | 1 |
| `TRAINS/TRAINSET/CDC_Rils_minipack/CDC_31_54_3541_022-2_Rils_ures.wag` | 64 |  | 1 |
| `TRAINS/TRAINSET/CDC_Rils_minipack/CDC_31_54_3541_083-7_Rils_rako.wag` | 64 |  | 1 |
| `TRAINS/TRAINSET/CDC_Rils_minipack/CDC_31_54_3541_083-7_Rils_ures.wag` | 64 |  | 1 |
| `TRAINS/TRAINSET/CDC_Rils_minipack/CDC_31_54_3541_101-4_Rils_ures.wag` | 64 |  | 1 |
| `TRAINS/TRAINSET/DB_VTG/Stare/DB_VTG_788_0_801-5_empty.wag` | 63 |  | 1 |
| `TRAINS/TRAINSET/DB_Wawel/DB_Avmz109.wag` | 23 |  | 1 |
| `TRAINS/TRAINSET/DB_Wawel/DB_Bpmz294.wag` | 23 |  | 1 |
| `TRAINS/TRAINSET/ED78_31WE/ORCZ_PREG_ED78-024P_A.eng` | 147 |  | 1 |
| `TRAINS/TRAINSET/ED78_31WE/ORCZ_PREG_ED78-024P_A1.eng` | 147 |  | 1 |
| `TRAINS/TRAINSET/ED78_31WE/ORCZ_PREG_ED78-024P_D.eng` | 137 |  | 1 |
| `TRAINS/TRAINSET/ED78_31WE/ORCZ_PREG_ED78-024P_D1.eng` | 137 |  | 1 |
| `TRAINS/TRAINSET/ED78_31WE/PREG_ED78-018_B.wag` | 30 |  | 1 |
| `TRAINS/TRAINSET/ED78_31WE/PREG_ED78-018_C.wag` | 30 |  | 1 |
| `TRAINS/TRAINSET/ED78_31WE/PREG_ED78-024P_B.wag` | 30 |  | 1 |
| `TRAINS/TRAINSET/ED78_31WE/PREG_ED78-024P_C.wag` | 30 |  | 1 |
| `TRAINS/TRAINSET/EN76_22WEa/KW_EN76-035P_B.wag` | 43 |  | 1 |
| `TRAINS/TRAINSET/EN76_22WEa/KW_EN76-035P_C.wag` | 38 |  | 1 |
| `TRAINS/TRAINSET/H-START CAF Minipack/H-START_19-91_102-3 Amz.wag` | 100 |  | 1 |
| `TRAINS/TRAINSET/H-START CAF Minipack/H-START_20-91_111-1 Bpmz.wag` | 100 |  | 1 |
| `TRAINS/TRAINSET/H-START CAF Minipack/H-START_21-91_102-9 Bmz.wag` | 100 |  | 1 |
| `TRAINS/TRAINSET/KS_EN75/KS_EN75_C_AI.eng` | 160 |  | 1 |
| `TRAINS/TRAINSET/KS_EN75/KS_EN75_D_AI.eng` | 160 |  | 1 |
| `TRAINS/TRAINSET/PKP IC RRT/RRT_RIC_0003.wag` | 314 |  | 1 |
| `TRAINS/TRAINSET/PKP_104Af-OR/PKP_Ab-39000.wag` | 24 |  | 1 |
| `TRAINS/TRAINSET/PKP_154A-OR/PKPIC_B10mnopuz-90021.wag` | 22 |  | 1 |
| `TRAINS/TRAINSET/PKP_154A-OR/PKPIC_B9mnopuvz-90010.wag` | 21 |  | 1 |
| `TRAINS/TRAINSET/PKP_155A-OR/PKPIC_BR4mnouz-71001.wag` | 23 |  | 1 |
| `TRAINS/TRAINSET/PKP_401K-OR/Stare/stare/PKP_Gas_1XT-ZK.wag` | 62 |  | 1 |
| `TRAINS/TRAINSET/PKP_401K-OR/Stare/stare/PKP_Gas_1XT-ZO.wag` | 62 |  | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/ITL_E186-136.eng` | 40 |  | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/LOTOS_E186-145.eng` | 40 |  | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/LOTOS_E186-145_p1.eng` | 40 |  | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/PR_E186-143.eng` | 40 |  | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/PR_E186-143_p1.eng` | 40 |  | 1 |
| `TRAINS/TRAINSET/PKP_ED250-OR/PKPIC_ED250-014d_2.wag` | 20 |  | 1 |
| `TRAINS/TRAINSET/PKP_ED250-OR/PKPIC_ED250-014d_3.wag` | 14 |  | 1 |
| `TRAINS/TRAINSET/PKP_ED250-OR/PKPIC_ED250-014e_2.wag` | 20 |  | 1 |
| `TRAINS/TRAINSET/PKP_ED250-OR/PKPIC_ED250-014e_3.wag` | 14 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PKPPR_EN57-687s.wag` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PKP_EN57-1192s.wag` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PKP_EN57-1326s.wag` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PKP_EN57-1575s.wag` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PKP_EN57-1797s.wag` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PKP_EN57-857rb.eng` | 27 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PKP_EN57-907s.wag` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PKP_EN57-945s.wag` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PREG_EN57-686s_zm.eng` | 17 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57/PKP_EN57-1326s.wag` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57/PKP_EN57-1575s.wag` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57/PKP_EN57-907s.wag` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57/PKP_EN57-945s.wag` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/EN57AL-1542s.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/EN57AL-1542s_v2.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PL_REG_EN57AL-2112s.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PREG_EN57AL-2103s.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PREG_EN57AL-2112s.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PREG_EN57AL-2117s.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PREG_EN57ALc-2207s.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PREG_EN57ALd-2219s.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s_v2.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112s.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103s.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117s.eng` | 25 |  | 1 |
| `TRAINS/TRAINSET/PKP_ET41_203E-OR/PKPC_ET41-048A_z.eng` | 13 |  | 1 |
| `TRAINS/TRAINSET/PKP_ET41_203E-OR/PKPC_ET41-048B_z.eng` | 14 |  | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/PKPC_EU43-002_pl.eng` | 46 |  | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/PKPC_EU43-003_pl.eng` | 40 |  | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/PREG_E186-143.eng` | 46 |  | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/RPOOL_E186-534.eng` | 45 |  | 1 |
| `TRAINS/TRAINSET/PKP_PPS-OR/OR_PKP_PPS_1.wag` | 109 |  | 1 |
| `TRAINS/TRAINSET/PKP_PPS-OR/OR_PKP_PPS_2.wag` | 109 |  | 1 |
| `TRAINS/TRAINSET/PKP_PPS-OR/OR_PKP_PPS_3.wag` | 69 |  | 1 |
| `TRAINS/TRAINSET/PKP_PPS-OR/PKPE_PPS-1224.wag` | 24 |  | 1 |
| `TRAINS/TRAINSET/PKP_PPS-OR/PKPE_PPS-1228.wag` | 30 |  | 1 |
| `TRAINS/TRAINSET/PKP_PPS-OR/PKPE_PPS-1229.wag` | 30 |  | 1 |
| `TRAINS/TRAINSET/PKP_SA110/PKP_SA112-03.wag` | 55 |  | 1 |
| `TRAINS/TRAINSET/PKP_Z1B-OR/PKPIC_B10bmnouz-90042.wag` | 21 |  | 1 |
| `TRAINS/TRAINSET/PKP_Z1B-OR/PKPIC_B11mnouz-90005.wag` | 21 |  | 1 |
| `TRAINS/TRAINSET/PKP_Z1B-OR/PKPIC_B11mnouz-90021.wag` | 22 |  | 1 |
| `TRAINS/TRAINSET/PKP_Z1B-OR/PKP_Bdmnu-90029.wag` | 22 |  | 1 |
| `TRAINS/TRAINSET/PKP_Zas/Stare/PKP_zas_785_2_736-2.wag` | 63 |  | 1 |
| `TRAINS/TRAINSET/PKP_Zas/Stare/PKP_zas_785_2_736-2_empty.wag` | 63 |  | 1 |
| `TRAINS/TRAINSET/RLB_Fals_Z-OR/RLB_Falns_VPS.wag` | 40 |  | 1 |
| `TRAINS/TRAINSET/RLB_Fals_Z-OR/RLB_Fals_KVG.wag` | 35 |  | 1 |
| `TRAINS/TRAINSET/RLB_Fals_Z-OR/RLB_Fals_KVG_voll.wag` | 36 |  | 1 |
| `TRAINS/TRAINSET/RLB_Fals_Z/RLB_Fals_KVG.wag` | 54 |  | 1 |
| `TRAINS/TRAINSET/RLB_Fals_Z/RLB_Fals_X_DBAG_VK.wag` | 54 |  | 1 |
| `TRAINS/TRAINSET/RLB_Fals_Z/RLB_Fals_X_DBAG_a.wag` | 54 |  | 1 |
| `TRAINS/TRAINSET/RLB_Fals_Z/RLB_Fals_Z.wag` | 54 |  | 1 |
| `TRAINS/TRAINSET/RLB_Fcs-OR/RLB_Fcs.wag` | 34 |  | 1 |
| `TRAINS/TRAINSET/RLB_Fcs-OR/RLB_Fcs_kalk.wag` | 35 |  | 1 |
| `TRAINS/TRAINSET/RLB_Fcs-OR/RLB_Fcs_kohle.wag` | 35 |  | 1 |
| `TRAINS/TRAINSET/RLB_Fcs-OR/RLB_Fcs_kohle.wag` | 36 |  | 1 |
| `TRAINS/TRAINSET/RLB_Tank_ATL-OR/Stare/RLB_Tank_ATL.wag` | 39 |  | 1 |
| `TRAINS/TRAINSET/RLB_Tank_ATL-OR/Stare/RLB_Tank_ATL.wag` | 40 |  | 1 |
| `TRAINS/TRAINSET/RLB_Tank_ATL-OR/Stare/RLB_Tank_ATL_voll.wag` | 39 |  | 1 |
| `TRAINS/TRAINSET/RLB_Tank_ATL-OR/Stare/RLB_Tank_ATL_voll.wag` | 40 |  | 1 |
| `TRAINS/TRAINSET/RLB_Tds-OR/Stare/RLB_DR_Tds_kalk_voll_rangier.wag` | 41 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp.wag` | 40 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp.wag` | 41 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_214.wag` | 40 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_214.wag` | 41 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_214_voll.wag` | 40 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_214_voll.wag` | 41 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_284_F.wag` | 40 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_284_F.wag` | 41 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_284_F_voll.wag` | 40 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_284_F_voll.wag` | 41 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_568.wag` | 40 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_568.wag` | 41 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_568_voll.wag` | 40 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_568_voll.wag` | 41 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_voll.wag` | 40 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/RLB_Slmmp_voll.wag` | 41 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/stare/RLB_Slmmp_rangier.wag` | 46 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/stare/RLB_Slmmp_rangier.wag` | 47 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/stare/RLB_Slmmp_rangier_voll.wag` | 46 |  | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/stare/RLB_Slmmp_rangier_voll.wag` | 47 |  | 1 |
| `TRAINS/TRAINSET/Sprzeg/sprzeg_ezt_sm.wag` | 36 |  | 1 |
| `TRAINS/TRAINSET/common.inc/Parowozy/Pm2/Adhesion_32D2-33_T.inc` | 20 |  | none |
| `TRAINS/TRAINSET/common.inc/Parowozy/Ty2/Lights_Ty2_T.inc` | 136 |  | none |
| `TRAINS/TRAINSET/common.inc/Parowozy/Ty2/Lights_Ty2_T.inc` | 137 |  | none |

### Include …: not found (418)

| File | Line | Detail | Vehicles |
| --- | --- | --- | --- |
| `TRAINS/TRAINSET/PCC_Fals/stare/PCC_Fals4.wag` | 13 | `TRAINS/TRAINSET/PCC_Fals/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PCC_Fals/stare/SK_AX_Benet_Fals-172_pusty.wag` | 7 | `TRAINS/TRAINSET/PCC_Fals/common.inc/Wagony/Towarowe/WAP/Size_WAP.inc` | 1 |
| `TRAINS/TRAINSET/PCC_Fals/stare/SK_AX_Benet_Fals-172_pusty.wag` | 8 | `TRAINS/TRAINSET/PCC_Fals/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PCC_Fals/stare/SK_AX_Benet_Fals-172_pusty.wag` | 9 | `TRAINS/TRAINSET/PCC_Fals/common.inc/Wagony/Towarowe/WAP/Brakes_WAP.inc` | 1 |
| `TRAINS/TRAINSET/PCC_Fals/stare/SK_AX_Benet_Fals-172_pusty.wag` | 10 | `TRAINS/TRAINSET/PCC_Fals/common.inc/Wagony/Towarowe/WAP/Adhesion_WAP.inc` | 1 |
| `TRAINS/TRAINSET/PCC_Fals/stare/SK_AX_Benet_Fals-172_wegiel.wag` | 7 | `TRAINS/TRAINSET/PCC_Fals/common.inc/Wagony/Towarowe/WAP/Size_WAP_80.inc` | 1 |
| `TRAINS/TRAINSET/PCC_Fals/stare/SK_AX_Benet_Fals-172_wegiel.wag` | 18 | `TRAINS/TRAINSET/PCC_Fals/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PCC_Fals/stare/SK_AX_Benet_Fals-172_wegiel.wag` | 19 | `TRAINS/TRAINSET/PCC_Fals/common.inc/Wagony/Towarowe/WAP/Brakes_WAP_80.inc` | 1 |
| `TRAINS/TRAINSET/PCC_Fals/stare/SK_AX_Benet_Fals-172_wegiel.wag` | 20 | `TRAINS/TRAINSET/PCC_Fals/common.inc/Wagony/Towarowe/WAP/Adhesion_WAP_80.inc` | 1 |
| `TRAINS/TRAINSET/PKOR_Vectron/OpenRailsCZSK/ID_6193-573_2026_PL.eng` | 17 | `TRAINS/TRAINSET/common.inc/Siemens/LIGHTS_Vectron_REF.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_E.wag` | 7 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Size_206S_E.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_E.wag` | 8 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_E.wag` | 9 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Brakes_206S_E.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_E.wag` | 10 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Adhesion_206S_E.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_E_E.wag` | 8 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Size_206S_E.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_E_E.wag` | 9 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_E_E.wag` | 10 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Brakes_206S_E.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_E_E.wag` | 11 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Adhesion_206S_E.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_L.wag` | 7 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Size_206S_L.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_L.wag` | 8 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_L.wag` | 9 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Brakes_206S_L.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_L.wag` | 10 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Adhesion_206S_L.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_L_E.wag` | 8 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Size_206S_L.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_L_E.wag` | 9 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_L_E.wag` | 10 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Brakes_206S_L.inc` | 1 |
| `TRAINS/TRAINSET/PKP_206S-OR/stare/PKPC_Ugpps-710_L_E.wag` | 11 | `TRAINS/TRAINSET/PKP_206S-OR/common.inc/Wagony/Towarowe/206S/Adhesion_206S_L.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_Dec_Zaes-238_empty.wag` | 7 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Size_406Ra.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_Dec_Zaes-238_empty.wag` | 8 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_Dec_Zaes-238_empty.wag` | 9 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Brakes_406Ra.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_Dec_Zaes-238_empty.wag` | 10 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Adhesion_406Ra.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_Dec_Zaes-238_full.wag` | 7 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Size_406Ra_80.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_Dec_Zaes-238_full.wag` | 8 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_Dec_Zaes-238_full.wag` | 9 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Brakes_406Ra_80.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_Dec_Zaes-238_full.wag` | 10 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Adhesion_406Ra_80.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_GATX_Zaes-006_empty.wag` | 7 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Size_406Ra.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_GATX_Zaes-006_empty.wag` | 8 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_GATX_Zaes-006_empty.wag` | 9 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Brakes_406Ra.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_GATX_Zaes-006_empty.wag` | 10 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Adhesion_406Ra.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_GATX_Zaes-006_full.wag` | 7 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Size_406Ra_80.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_GATX_Zaes-006_full.wag` | 8 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_GATX_Zaes-006_full.wag` | 9 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Brakes_406Ra_80.inc` | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_GATX_Zaes-006_full.wag` | 10 | `TRAINS/TRAINSET/PKP_406Ra-OR/common.inc/Wagony/Towarowe/406Ra/Adhesion_406Ra_80.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/AKIEM_186353.eng` | 124 | `TRAINS/TRAINSET/PKP_E186_TRAXX/LIGHTS/LIGHTS-AKIEM186364-CZ.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145.eng` | 7 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/wymiar.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145.eng` | 24 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145.eng` | 25 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/hamulec.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145.eng` | 26 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/fizyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145.eng` | 27 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/obwod_WN.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145.eng` | 29 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/Lights_E186_v2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145.eng` | 33 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/parametr.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145.eng` | 34 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/pneumatyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145.eng` | 35 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/TCS_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145.eng` | 36 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/sterowanie.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p1.eng` | 7 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/wymiar.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p1.eng` | 24 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p1.eng` | 25 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/hamulec.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p1.eng` | 26 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/fizyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p1.eng` | 27 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/obwod_WN.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p1.eng` | 29 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/Lights_E186_v2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p1.eng` | 33 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/parametr.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p1.eng` | 34 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/pneumatyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p1.eng` | 35 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/TCS_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p1.eng` | 36 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/sterowanie.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p2.eng` | 7 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/wymiar.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p2.eng` | 24 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p2.eng` | 25 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/hamulec.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p2.eng` | 26 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/fizyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p2.eng` | 27 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/obwod_WN.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p2.eng` | 29 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/Lights_E186_v2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p2.eng` | 33 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/parametr.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p2.eng` | 34 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/pneumatyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p2.eng` | 35 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/TCS_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p2.eng` | 36 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/sterowanie.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p3.eng` | 7 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/wymiar.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p3.eng` | 24 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p3.eng` | 25 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/hamulec.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p3.eng` | 26 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/fizyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p3.eng` | 27 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/obwod_WN.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p3.eng` | 29 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/Lights_E186_v2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p3.eng` | 33 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/parametr.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p3.eng` | 34 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/pneumatyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p3.eng` | 35 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/TCS_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/LOTOS_E186-145_p3.eng` | 36 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/sterowanie.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143.eng` | 7 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/wymiar.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143.eng` | 24 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143.eng` | 25 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/hamulec.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143.eng` | 26 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/fizyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143.eng` | 27 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/obwod_WN.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143.eng` | 29 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/Lights_E186_v2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143.eng` | 33 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/parametr.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143.eng` | 34 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/pneumatyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143.eng` | 35 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/TCS_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143.eng` | 36 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/sterowanie.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p1.eng` | 7 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/wymiar.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p1.eng` | 24 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p1.eng` | 25 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/hamulec.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p1.eng` | 26 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/fizyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p1.eng` | 27 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/obwod_WN.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p1.eng` | 29 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/Lights_E186_v2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p1.eng` | 33 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/parametr.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p1.eng` | 34 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/pneumatyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p1.eng` | 35 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/TCS_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p1.eng` | 36 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/sterowanie.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p2.eng` | 7 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/wymiar.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p2.eng` | 24 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p2.eng` | 25 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/hamulec.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p2.eng` | 26 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/fizyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p2.eng` | 27 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/obwod_WN.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p2.eng` | 29 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/Lights_E186_v2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p2.eng` | 33 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/parametr.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p2.eng` | 34 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/pneumatyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p2.eng` | 35 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/TCS_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p2.eng` | 36 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/sterowanie.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p3.eng` | 7 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/wymiar.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p3.eng` | 24 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p3.eng` | 25 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/hamulec.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p3.eng` | 26 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/fizyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p3.eng` | 27 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/obwod_WN.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p3.eng` | 29 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/Lights_E186_v2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p3.eng` | 33 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/parametr.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p3.eng` | 34 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/pneumatyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p3.eng` | 35 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/TCS_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_E186_TRAXX/Stare/PREG_E186-143_p3.eng` | 36 | `TRAINS/TRAINSET/PKP_E186_TRAXX/common.inc/Lokomotywy/E186/sterowanie.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 19 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 20 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 21 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 22 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 23 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 24 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Pantographs.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 34 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Lights2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 40 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Parametr_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 41 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Compressor.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 42 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Pneumatics.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 43 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra.eng` | 44 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Controlling.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 19 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 20 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 21 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 22 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 23 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 24 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Pantographs.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 42 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Lights2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 48 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Parametr_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 49 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Compressor.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 50 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Pneumatics.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 51 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542ra_v2.eng` | 52 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Controlling.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 8 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 9 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 10 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 11 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 12 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 13 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Pantographs.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 23 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Lights2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 29 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Parametr_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 30 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Compressor.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 31 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Pneumatics.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 32 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb.eng` | 33 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Controlling.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 8 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 9 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 10 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 11 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 12 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 13 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Pantographs.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 31 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Lights2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 37 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Parametr_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 38 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Compressor.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 39 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Pneumatics.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 40 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542rb_v2.eng` | 41 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Controlling.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s.eng` | 8 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s.eng` | 9 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s.eng` | 10 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s.eng` | 11 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s.eng` | 12 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s_v2.eng` | 8 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s_v2.eng` | 9 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s_v2.eng` | 10 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s_v2.eng` | 11 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/EN57AL-1542s_v2.eng` | 12 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 19 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 20 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 21 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 22 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 23 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 24 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Pantographs.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 42 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Lights2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 48 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Parametr_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 49 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Compressor.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 50 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Pneumatics.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 51 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112ra.eng` | 52 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Controlling.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 8 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 9 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 10 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 11 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 12 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 13 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Pantographs.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 31 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Lights2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 37 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Parametr_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 38 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Compressor.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 39 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Pneumatics.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 40 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112rb.eng` | 41 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Controlling.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112s.eng` | 8 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112s.eng` | 9 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112s.eng` | 10 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112s.eng` | 11 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PL_REG_EN57AL-2112s.eng` | 12 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 19 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 20 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 21 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 22 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 23 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 24 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Pantographs.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 42 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Lights2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 48 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Parametr_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 49 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Compressor.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 50 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Pneumatics.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 51 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103ra.eng` | 52 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Controlling.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 8 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 9 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 10 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 11 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 12 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 13 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Pantographs.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 31 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Lights2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 37 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Parametr_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 38 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Compressor.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 39 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Pneumatics.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 40 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103rb.eng` | 41 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Controlling.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103s.eng` | 8 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103s.eng` | 9 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103s.eng` | 10 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103s.eng` | 11 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2103s.eng` | 12 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 19 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 20 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 21 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 22 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 23 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 24 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Pantographs.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 42 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Lights2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 48 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Parametr_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 49 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Compressor.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 50 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Pneumatics.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 51 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117ra.eng` | 52 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Controlling.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 8 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 9 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 10 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 11 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 12 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_r.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 13 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Pantographs.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 31 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Lights2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 37 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Parametr_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 38 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Compressor.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 39 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Pneumatics.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 40 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117rb.eng` | 41 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Controlling.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117s.eng` | 8 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Size_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117s.eng` | 9 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZT_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117s.eng` | 10 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/BrakeForce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117s.eng` | 11 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Parameters Constantly_PL/EZTA_Brakes.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/xhl/PREG_EN57AL-2117s.eng` | 12 | `TRAINS/TRAINSET/PKP_EN57A-OR/common.inc/Lokomotywy/EN57A/Adhesion_s.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/Stary eng/PREG_E186-143.eng` | 6 | `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/common.inc/Lokomotywy/EU43_TRAXX/Size_TRAXX.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/Stary eng/PREG_E186-143.eng` | 29 | `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/Stary eng/PREG_E186-143.eng` | 30 | `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/common.inc/Lokomotywy/EU43_TRAXX/Brakes_TRAXX.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/Stary eng/PREG_E186-143.eng` | 31 | `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/common.inc/Lokomotywy/EU43_TRAXX/Adhesion_TRAXX.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/Stary eng/PREG_E186-143.eng` | 33 | `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/common.inc/Lokomotywy/EU43_TRAXX/Lights_TRAXX.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/Stary eng/PREG_E186-143.eng` | 37 | `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/common.inc/Lokomotywy/EU43_TRAXX/Parametr_TRAXX_F140.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/Stary eng/PREG_E186-143.eng` | 38 | `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/common.inc/TCS_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/Stary eng/PREG_E186-143.eng` | 39 | `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/common.inc/Lokomotywy/EU43_TRAXX/Controlling_TRAXX.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/Stary eng/PREG_E186-143_cz.eng` | 132 | `TRAINS/TRAINSET/PKP_EU43_TRAXX-OR/common.inc/Lokomotywy/EU43_TRAXX/Lights_TRAXX.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/MRCE_PREG_ES64F4-458_P1.eng` | 6 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Size_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/MRCE_PREG_ES64F4-458_P1.eng` | 23 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/MRCE_PREG_ES64F4-458_P1.eng` | 24 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Brakes_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/MRCE_PREG_ES64F4-458_P1.eng` | 25 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Adhesion_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/MRCE_PREG_ES64F4-458_P1.eng` | 26 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Pantograph_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/MRCE_PREG_ES64F4-458_P1.eng` | 28 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Lights_ES64F4_v2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/MRCE_PREG_ES64F4-458_P1.eng` | 32 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Parametr_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/MRCE_PREG_ES64F4-458_P1.eng` | 33 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/TCS_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/MRCE_PREG_ES64F4-458_P1.eng` | 34 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Controlling_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/PKP-EU45-846_DE_P1.eng` | 6 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Size_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/PKP-EU45-846_DE_P1.eng` | 23 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/PKP-EU45-846_DE_P1.eng` | 24 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Brakes_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/PKP-EU45-846_DE_P1.eng` | 25 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Adhesion_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/PKP-EU45-846_DE_P1.eng` | 26 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Pantograph_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/PKP-EU45-846_DE_P1.eng` | 28 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Lights_ES64F4_v2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/PKP-EU45-846_DE_P1.eng` | 32 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Parametr_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/PKP-EU45-846_DE_P1.eng` | 33 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/TCS_PL.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/Stare/PKP-EU45-846_DE_P1.eng` | 34 | `TRAINS/TRAINSET/PKP_EU45_ES64F4-OR/common.inc/Lokomotywy/EU45_ES64F4/Controlling_ES64F4.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 19 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Siemens/udaje_VectronMS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 20 | `TRAINS/TRAINSET/PKP_EU46/common.inc/spriahlo/0cm.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 21 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Siemens/adhezia_odpor_VectronMS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 22 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Siemens/brzdaP_VectronMS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 23 | `TRAINS/TRAINSET/PKP_EU46/common.inc/brzdna_krivka/kotouce.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 24 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Siemens/zberace_VectronMS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 26 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Siemens/LIGHTS_Vectron_CZ1.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 31 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Siemens/parametre_VectronMS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 32 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Siemens/krivky_tch_edb_VectronMS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 33 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Siemens/vzduchotechnika193.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 34 | `TRAINS/TRAINSET/PKP_EU46/common.inc/brzdice/BPE.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 36 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Mirel_VZ/vectron.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 37 | `TRAINS/TRAINSET/PKP_EU46/common.inc/ARR/383.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKPC_370024.eng` | 38 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Siemens/ovladanie_VectronMS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 7 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Size_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 26 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Standard_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 27 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/BrakeForce_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 28 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Brakes_S.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 29 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Adhesion_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 30 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Pantographs_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 32 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Lights2_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 38 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Parametr_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 39 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Compressor_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 40 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Pneumatics_S.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 41 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-503.eng` | 42 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Controlling_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 7 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Size_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 26 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Standard_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 27 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/BrakeForce_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 28 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Brakes_S.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 29 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Adhesion_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 30 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Pantographs_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 32 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Lights2_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 38 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Parametr_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 39 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Compressor_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 40 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Pneumatics_S.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 41 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-512.eng` | 42 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Controlling_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 8 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Size_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 27 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Standard_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 28 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/BrakeForce_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 29 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Brakes_S.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 30 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Adhesion_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 31 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Pantographs_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 33 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Lights2_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 39 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Parametr_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 40 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Compressor_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 41 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Pneumatics_S.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 42 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-521.eng` | 43 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Controlling_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 8 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Size_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 27 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Standard_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 28 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/BrakeForce_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 29 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Brakes_S.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 30 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Adhesion_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 31 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Pantographs_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 33 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Lights2_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 39 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Parametr_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 40 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Compressor_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 41 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/Pneumatics_S.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 42 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_EU46/Old/PKP_EU46-522.eng` | 43 | `TRAINS/TRAINSET/PKP_EU46/common.inc/Lokomotywy/EU46/Controlling_EU46.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM42_6D-OR/PKP_SM42-124_zm.eng` | 25 | `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Lights_TypeI_zm_zm.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_flip.eng` | 37 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Size_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_flip.eng` | 38 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/Standard_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_flip.eng` | 39 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/BrakeForce_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_flip.eng` | 40 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/Brakes_O.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_flip.eng` | 41 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Adhesion_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_flip.eng` | 43 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Lights_TypeII_SM48_flip.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_flip.eng` | 51 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Effects_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_flip.eng` | 52 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Agregat_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_flip.eng` | 53 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/Pneumatics_O.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_flip.eng` | 54 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_flip.eng` | 55 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Controlling_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_m.eng` | 37 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Size_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_m.eng` | 38 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/Standard_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_m.eng` | 39 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/BrakeForce_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_m.eng` | 40 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/Brakes_O.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_m.eng` | 41 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Adhesion_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_m.eng` | 43 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Lights_TypeII_SM48_m.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_m.eng` | 51 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Effects_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_m.eng` | 52 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Agregat_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_m.eng` | 53 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/Pneumatics_O.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_m.eng` | 54 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_m.eng` | 55 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Controlling_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_zm.eng` | 25 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Size_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_zm.eng` | 26 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/Standard_Coupler.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_zm.eng` | 27 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/BrakeForce_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_zm.eng` | 28 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/Brakes_O.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_zm.eng` | 29 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Adhesion_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_zm.eng` | 30 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Lights_TypeII_SM48_zm.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_zm.eng` | 38 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Effects_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_zm.eng` | 39 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Agregat_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_zm.eng` | 40 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/Pneumatics_O.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_zm.eng` | 41 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Parameters Constantly_PL/TCS.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SM48-OR/Docs/PKP_SM48-103_zm.eng` | 42 | `TRAINS/TRAINSET/PKP_SM48-OR/common.inc/Lokomotywy/SM48/Controlling_SM48.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SP42_101D-OR/PKP_SP42-002_zm.eng` | 40 | `TRAINS/TRAINSET/common.inc/Lokomotywy/SP42/Effects.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SP42_101D-OR/PKP_SP42-117_zm.eng` | 40 | `TRAINS/TRAINSET/common.inc/Lokomotywy/SP42/Effects.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SP42_101D-OR/PKP_SP42-210_zm.eng` | 40 | `TRAINS/TRAINSET/common.inc/Lokomotywy/SP42/Effects.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SP42_101D-OR/PKP_SP42-213_zm.eng` | 40 | `TRAINS/TRAINSET/common.inc/Lokomotywy/SP42/Effects.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SP42_101D-OR/PKP_SP42-214_zm.eng` | 40 | `TRAINS/TRAINSET/common.inc/Lokomotywy/SP42/Effects.inc` | 1 |
| `TRAINS/TRAINSET/PKP_SP42_101D-OR/PKP_SP42-231_zm.eng` | 40 | `TRAINS/TRAINSET/common.inc/Lokomotywy/SP42/Effects.inc` | 1 |
| `TRAINS/TRAINSET/PKP_ST43-OR/ST43-R004.eng` | 16 | `TRAINS/TRAINSET/common.inc/Lokomotywy/ST43/wymiar.inc` | 1 |
| `TRAINS/TRAINSET/PKP_ST43-OR/ST43-R004.eng` | 17 | `TRAINS/TRAINSET/common.inc/Lokomotywy/ST43/sprzeg.inc` | 1 |
| `TRAINS/TRAINSET/PKP_ST43-OR/ST43-R004.eng` | 18 | `TRAINS/TRAINSET/common.inc/Lokomotywy/ST43/fizyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_ST43-OR/ST43-R004.eng` | 19 | `TRAINS/TRAINSET/common.inc/Lokomotywy/ST43/hamulec.inc` | 1 |
| `TRAINS/TRAINSET/PKP_ST43-OR/ST43-R004.eng` | 20 | `TRAINS/TRAINSET/common.inc/Lokomotywy/ST43/swiatla_2.inc` | 1 |
| `TRAINS/TRAINSET/PKP_ST43-OR/ST43-R004.eng` | 24 | `TRAINS/TRAINSET/common.inc/Lokomotywy/ST43/parametry.inc` | 1 |
| `TRAINS/TRAINSET/PKP_ST43-OR/ST43-R004.eng` | 25 | `TRAINS/TRAINSET/common.inc/Lokomotywy/ST43/pneumatyka.inc` | 1 |
| `TRAINS/TRAINSET/PKP_ST43-OR/ST43-R004.eng` | 27 | `TRAINS/TRAINSET/common.inc/Lokomotywy/ST43/tcs.inc` | 1 |
| `TRAINS/TRAINSET/PKP_ST43-OR/ST43-R004.eng` | 28 | `TRAINS/TRAINSET/common.inc/Lokomotywy/ST43/sterowanie.inc` | 1 |
| `TRAINS/TRAINSET/PKP_ST43-OR/ST43-R004.eng` | 30 | `TRAINS/TRAINSET/common.inc/Lokomotywy/ST43/inne.inc` | 1 |
| `TRAINS/TRAINSET/RAILL_193-031/OpenRailsCZSK/RAILL_193031.eng` | 31 | `TRAINS/TRAINSET/common.inc/Siemens/zberace_VectronMSPL.inc` | 1 |
| `TRAINS/TRAINSET/RAILL_193-031/OpenRailsCZSK/RAILL_193031.eng` | 33 | `TRAINS/TRAINSET/common.inc/Siemens/LIGHTS_Vectron_REF.inc` | 1 |
| `TRAINS/TRAINSET/RAILL_193-682/OpenRailsCZSK/RAILL_193682.eng` | 31 | `TRAINS/TRAINSET/common.inc/Siemens/zberace_VectronMSPL.inc` | 1 |
| `TRAINS/TRAINSET/RAILL_193-682/OpenRailsCZSK/RAILL_193682.eng` | 33 | `TRAINS/TRAINSET/common.inc/Siemens/LIGHTS_Vectron_REF.inc` | 1 |
| `TRAINS/TRAINSET/RAILL_193-683/OpenRailsCZSK/RAILL_193683.eng` | 31 | `TRAINS/TRAINSET/common.inc/Siemens/zberace_VectronMSPL.inc` | 1 |
| `TRAINS/TRAINSET/RAILL_193-683/OpenRailsCZSK/RAILL_193683.eng` | 33 | `TRAINS/TRAINSET/common.inc/Siemens/LIGHTS_Vectron_REF.inc` | 1 |
| `TRAINS/TRAINSET/RLB_Tank_ATL-OR/Stare/RLB_Tank_ATL.wag` | 11 | `TRAINS/TRAINSET/RLB_Tank_ATL-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/RLB_Tank_ATL-OR/Stare/RLB_Tank_ATL_voll.wag` | 11 | `TRAINS/TRAINSET/RLB_Tank_ATL-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/RLB_Tds-OR/Stare/RLB_DR_Tds_kalk_voll_rangier.wag` | 12 | `TRAINS/TRAINSET/RLB_Tds-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/stare/RLB_Slmmp_rangier.wag` | 12 | `TRAINS/TRAINSET/RLB_slmmp-OR/common.inc/Standard_Coupler_PL.inc` | 1 |
| `TRAINS/TRAINSET/RLB_slmmp-OR/stare/RLB_Slmmp_rangier_voll.wag` | 12 | `TRAINS/TRAINSET/RLB_slmmp-OR/common.inc/Standard_Coupler_PL.inc` | 1 |

### Its parentheses do not balance: read where it is included, as Open Rails reads it, closing or leaving open the block around it (5)

| File | Line | Detail | Vehicles |
| --- | --- | --- | --- |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/31WE/Lights_typ2G_ORCZSK.inc` | 1303 |  | 4 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU43_TRAXX/Parametr_TRAXX_F140.inc` | 105 |  | 10 |
| `TRAINS/TRAINSET/common.inc/Wagony/Osobowe/154A/Lights_154A.inc` | 190 |  | 2 |
| `TRAINS/TRAINSET/common.inc/Wagony/Osobowe/DB_BWE/Lights_DB_BWE.inc` | 190 |  | 2 |
| `TRAINS/TRAINSET/common.inc/Wagony/Osobowe/Z1B/Lights_Z1B.inc` | 189 |  | 4 |

### The block … is not closed at the end of the file (46)

| File | Line | Detail | Vehicles |
| --- | --- | --- | --- |
| `TRAINS/TRAINSET/B-BLG_Laaers700_BTS/BLG_Laaers_B_Yaris-E.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/CD_KC_Eas/KC_Eas_356_ru.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/CD_KC_Eas/KC_Eas_356_ru_k.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/PCC_Fals/PCC_Fals_9055_Pro.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/PCC_Fals/PCC_Fals_9055_lad.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP EN57_AA-OR/Stare/PKPEN57-1752ra.eng` | 791 |  | 1 |
| `TRAINS/TRAINSET/PKP EN57_AA-OR/Stare/PKPEN57-1752rb.eng` | 791 |  | 1 |
| `TRAINS/TRAINSET/PKP EN57_AA-OR/Stare/PKPEN57-1752s.eng` | 791 |  | 1 |
| `TRAINS/TRAINSET/PKP EN57_AA-OR/Stare/PKPEN57-1943ra.eng` | 791 |  | 1 |
| `TRAINS/TRAINSET/PKP EN57_AA-OR/Stare/PKPEN57-1943rb.eng` | 791 |  | 1 |
| `TRAINS/TRAINSET/PKP EN57_AA-OR/Stare/PKPEN57-1943s.eng` | 791 |  | 1 |
| `TRAINS/TRAINSET/PKP EN57_AA-OR/Stare/PKPEN57-895ra.eng` | 791 |  | 1 |
| `TRAINS/TRAINSET/PKP EN57_AA-OR/Stare/PKPEN57-895rb.eng` | 791 |  | 1 |
| `TRAINS/TRAINSET/PKP EN57_AA-OR/Stare/PKPEN57-895s.eng` | 791 |  | 1 |
| `TRAINS/TRAINSET/PKP_305A-OR/PKPIC_WLAB10mnouz-90002.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_Dec_Zaes-238_empty.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_Dec_Zaes-238_full.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_GATX_Zaes-006_empty.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_406Ra-OR/Stare/PL_GATX_Zaes-006_full.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PREG_EN57-1018ra_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PREG_EN57-1018rb_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PREG_EN57-1018s_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PREG_EN57-686ra_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57-OR/PREG_EN57-686rb_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/KSL_EN57-730ra_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/KSL_EN57-730rb_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/KSL_EN57-730s_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PREG_EN57-1730ra_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PREG_EN57-1730ra_zm_v2.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PREG_EN57-1730rb_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PREG_EN57-1730rb_zm_v2.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PREG_EN57-1730s_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57A-OR/PREG_EN57-1730s_zm_v2.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57SPOT-OR/PREG_EN57-2076ra_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57SPOT-OR/PREG_EN57-2076rb_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_EN57SPOT-OR/PREG_EN57-2076s_zm.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_ET22_201E-OR/RAILP_201E0-005.eng` | 3 |  | 1 |
| `TRAINS/TRAINSET/PKP_WM15A-OR/PKP_WM15A-266.stat.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/ZSCS125/125815.eng` | 547 |  | 1 |
| `TRAINS/TRAINSET/ZSCS125/125816.eng` | 547 |  | 1 |
| `TRAINS/TRAINSET/ZSSKCvagFalls/zsskc_Falls_020_prazdny_k.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/ZSSKCvagFalls/zsskc_Falls_020_uhlie_k.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/cd_Ampz_002/cd_Ampz_002.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/cd_WRmz_006/cd_WRmz_006-i.wag` | 3 |  | 1 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM03_Ls150/Controlling_Ls150.inc` | 5 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Lights_6D_manzrobic.inc` | 2 |  | none |

### The first line leaves a quote open or closes a block it did not open: not read, as Open Rails never reads it (12)

| File | Line | Detail | Vehicles |
| --- | --- | --- | --- |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ED250/Size_p.inc` | 1 | `"comment ("Size, InteriaTensor, Mass etc. dla modelu typ ED250 wag_0)"` | 2 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM31/Size_SM31.inc` | 1 | `"comment ("Size, InteriaTensor, Mass etc. dla modelu SM31)"` | 30 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM31_411D/Size_SM31.inc` | 1 | `"comment ("Size, InteriaTensor, Mass etc. dla modelu SM31)"` | 3 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Effects.inc` | 1 | `"comment ("Effect dla lokomotyw SM42 SU42 )"` | 58 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Effects_101D.inc` | 1 | `"comment ("Effect dla lokomotyw 101D z ogrzewaniem parowym )"` | 42 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Effects_6D.inc` | 1 | `"comment ("Effect dla lokomotyw SM42 SU42 )"` | 51 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SP32/Size_SP32.inc` | 1 | `"comment ("Size, InteriaTensor, Mass etc. dla modelu SP32)"` | 27 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SP42/Effects_SP.inc` | 1 | `"comment ("Effect dla lokomotyw 101D z ogrzewaniem parowym )"` | 11 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SU42/Effects.inc` | 1 | `"comment ("Effect dla lokomotyw SM42 SU42 )"` | none |
| `TRAINS/TRAINSET/common.inc/Parowozy/Pt47/Effects_Pt47-55.inc` | 1 | `"comment ("Effect dla parowozu Pt47 )"` | none |
| `TRAINS/TRAINSET/common.inc/Parowozy/Pt47/Effects_Pt47.inc` | 1 | `"comment ("Effect dla parowozu Pt47 )"` | none |
| `TRAINS/TRAINSET/common.inc/Parowozy/Ty2/Effects_Ty2.inc` | 1 | `"comment ("Effect dla parowozu Ty2 )"` | none |

### Unterminated quoted string (2)

| File | Line | Detail | Vehicles |
| --- | --- | --- | --- |
| `TRAINS/TRAINSET/ZSCS125/125815.eng` | 771 |  | 1 |
| `TRAINS/TRAINSET/ZSCS125/125816.eng` | 770 |  | 1 |

## To look at (56)

Read here, but Open Rails reads it differently (a first line), or the text's encoding is a guess.

### Data on the first line, which Open Rails does not read (it takes it for the file's header) (12)

| File | Line | Detail | Vehicles |
| --- | --- | --- | --- |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/22WEa/Lights.inc` | 1 | `"*************************************************************************************"` | 2 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/22WEa/Lights_Pc2.inc` | 1 | `"*************************************************************************************"` | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/31WE/Lights_typ1G.inc` | 1 | `"*************************************************************************************"` | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_4E/Lights_TypII_4E.inc` | 1 | `"-----------------------------------------------------------------------------------"` | 6 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SA134/Agregat_SA134_B.inc` | 1 | `"ORTSDieselEngines ( 1"` | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Lights3_TypeI.inc` | 1 | `"b  ß00 hbcomment ("Lights SM42 Typ I <1988r")"` | 2 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Lights3_TypeI_zm.inc` | 1 | `"b  ß00 hbcomment ("Lights SM42 Typ I <1988r")"` | 2 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Lights4_TypeI.inc` | 1 | `"b  ß00 hbcomment ("Lights SM42 Typ I <1988r")"` | 5 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Lights4_TypeI_zm.inc` | 1 | `"b  ß00 hbcomment ("Lights SM42 Typ I <1988r")"` | 2 |
| `TRAINS/TRAINSET/common.inc/Parowozy/Tkt48/Effects_Tkt48.inc` | 1 | `"9comment ("Effect dla parowozu Tkt48")"` | none |
| `TRAINS/TRAINSET/common.inc/Parowozy/Tp4/Effects_Tp4.inc` | 1 | `"9comment ("Effect dla parowozu Tp4")"` | none |
| `TRAINS/TRAINSET/common.inc/Parowozy/Tw1/Effects_Tp4.inc` | 1 | `"9comment ("Effect dla parowozu Tp4")"` | none |

### Neither UTF-… nor UTF-…: read as Latin-… (44)

| File | Line | Detail | Vehicles |
| --- | --- | --- | --- |
| `TRAINS/TRAINSET/ZSSKC751/751192.inc` | 0 |  | 1 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/E193_VECTRON/svetlaCZ_VectronMS.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/E193_VECTRON/svetlaSK_VectronMS.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/E193_VECTRON/svetlaU_VectronMS.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET21_3E/Lights_3E.inc` | 0 |  | 4 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET21_3E/Lights_3E_Pc2.inc` | 0 |  | 7 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET21_3E/Lights_3E_z.inc` | 0 |  | 5 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET21_3E/Lights_TypII_3E_z.inc` | 0 |  | 2 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET40/Lights_ET40.inc` | 0 |  | 4 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET40/Lights_ET40_Pc2.inc` | 0 |  | 4 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET40/Lights_ET40_z.inc` | 0 |  | 3 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET41_203E/Lights_203E.inc` | 0 |  | 8 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET41_203E/Lights_203E_Pc2.inc` | 0 |  | 8 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET41_203E/Lights_203E_Pc2_v1.inc` | 0 |  | 8 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET41_203E/Lights_203E_v1.inc` | 0 |  | 8 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET41_203E/Lights_203E_z.inc` | 0 |  | 8 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/ET41_203E/Lights_203E_z_v1.inc` | 0 |  | 8 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU06_4E/Lights_TypII_4E.inc` | 0 |  | 4 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU06_4E/Lights_TypII_4E_Pc2.inc` | 0 |  | 4 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU06_4E/Lights_TypII_4E_z.inc` | 0 |  | 4 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_303E/Lights4_TypII_303E.inc` | 0 |  | 1 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_303E/Lights_303E_v1.inc` | 0 |  | 3 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_303E/Lights_303E_v1_Pc2.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_303E/Lights_303E_v1_z.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_303E/Lights_TypII_303E_Pc2.inc` | 0 |  | 4 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_303E/Lights_TypII_303E_z.inc` | 0 |  | 4 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_4E/Lights_4E_v1.inc` | 0 |  | 16 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_4E/Lights_4E_v1_Pc2.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_4E/Lights_4E_v1_z.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_4E/Lights_TypII_4E_Pc2.inc` | 0 |  | 2 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/EU07_4E/Lights_TypII_4E_z.inc` | 0 |  | 3 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Lights3_TypeI.inc` | 0 |  | 2 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Lights3_TypeI_zm.inc` | 0 |  | 2 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Lights4_TypeI.inc` | 0 |  | 5 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/SM42_6D/Lights4_TypeI_zm.inc` | 0 |  | 2 |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/X6X/svetlaCZ3.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/X6X/svetlaSK1.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/X6X/svetlaSK1_263.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/X6X/svetlaSK2.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Lokomotywy/X6X/svetlaSK2_263.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Parowozy/OKo1/Lights_OKo1.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Parowozy/OKo1/Lights_OKo1_v1.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Parowozy/TKw2/Lights_TKw1.inc` | 0 |  | none |
| `TRAINS/TRAINSET/common.inc/Parowozy/TKw2/Lights_TKw2.inc` | 0 |  | none |

