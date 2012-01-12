/*
    Sylverant Ship Server
    Copyright (C) 2010, 2012 Lawrence Sebald

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License version 3
    as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>

#include "items.h"

/* We need LE32 down below... so get it from packets.h */
#define PACKETS_H_HEADERS_ONLY
#include "packets.h"

/* Master list of all items. Spelling in this list is as it is in PSOPC. */
static item_map_t item_list[] = {
    { Item_Meseta, "Meseta" },
    { Item_Saber, "Saber" },
    { Item_Brand, "Brand" },
    { Item_Buster, "Buster" },
    { Item_Pallasch, "Pallasch" },
    { Item_Gladius, "Gladius" },
    { Item_DBS_SABER, "DB'S SABER" },
    { Item_KALADBOLG, "KALADBOLG" },
    { Item_DURANDAL, "DURANDAL" },
    { Item_Sword, "Sword" },
    { Item_Gigush, "Gigush" },
    { Item_Breaker, "Breaker" },
    { Item_Claymore, "Claymore" },
    { Item_Calibur, "Calibur" },
    { Item_FLOWENS_SWORD, "FLOWEN'S SWORD" },
    { Item_LAST_SURVIVOR, "LAST SURVIVOR" },
    { Item_DRAGON_SLAYER, "DRAGON SLAYER" },
    { Item_Dagger, "Dagger" },
    { Item_Knife, "Knife" },
    { Item_Blade, "Blade" },
    { Item_Edge, "Edge" },
    { Item_Ripper, "Ripper" },
    { Item_BLADE_DANCE, "BLADE DANCE" },
    { Item_BLOODY_ART, "BLOODY ART" },
    { Item_CROSS_SCAR, "CROSS SCAR" },
    { Item_Partisan, "Partisan" },
    { Item_Halbert, "Halbert" },
    { Item_Glaive, "Glaive" },
    { Item_Berdys, "Berdys" },
    { Item_Gungnir, "Gungnir" },
    { Item_BRIONAC, "BRIONAC" },
    { Item_VJAYA, "VJAYA" },
    { Item_GAE_BOLG, "GAE BOLG" },
    { Item_Slicer, "Slicer" },
    { Item_Spinner, "Spinner" },
    { Item_Cutter, "Cutter" },
    { Item_Sawcer, "Sawcer" },
    { Item_Diska, "Diska" },
    { Item_SLICER_OF_ASSASSIN, "SLICER OF ASSASSIN" },
    { Item_DISKA_OF_LIBERATOR, "DISKA OF LIBERATOR" },
    { Item_DISKA_OF_BRAVEMAN, "DISKA OF BRAVEMAN" },
    { Item_Handgun, "Handgun" },
    { Item_Autogun, "Autogun" },
    { Item_Lockgun, "Lockgun" },
    { Item_Railgun, "Railgun" },
    { Item_Raygun, "Raygun" },
    { Item_VARISTA, "VARISTA" },
    { Item_CUSTOM_RAY_ver_OO, "CUSTOM RAY ver.OO" },
    { Item_BRAVACE, "BRAVACE" },
    { Item_Rifle, "Rifle" },
    { Item_Sniper, "Sniper" },
    { Item_Blaster, "Blaster" },
    { Item_Beam, "Beam" },
    { Item_Laser, "Laser" },
    { Item_VISK_235W, "VISK-235W" },
    { Item_WALS_MK2, "WALS-MK2" },
    { Item_JUSTY_23ST, "JUSTY-23ST" },
    { Item_Mechgun, "Mechgun" },
    { Item_Assault, "Assault" },
    { Item_Repeater, "Repeater" },
    { Item_Gatling, "Gatling" },
    { Item_Vulcan, "Vulcan" },
    { Item_M_and_A60_VISE, "M&A60 VISE" },
    { Item_H_and_S25_JUSTICE, "H&S25 JUSTICE" },
    { Item_L_and_K14_COMBAT, "L&K14 COMBAT" },
    { Item_Shot, "Shot" },
    { Item_Spread, "Spread" },
    { Item_Cannon, "Cannon" },
    { Item_Launcher, "Launcher" },
    { Item_Arms, "Arms" },
    { Item_CRUSH_BULLET, "CRUSH BULLET" },
    { Item_METEOR_SMASH, "METEOR SMASH" },
    { Item_FINAL_IMPACT, "FINAL IMPACT" },
    { Item_Cane, "Cane" },
    { Item_Stick, "Stick" },
    { Item_Mace, "Mace" },
    { Item_Club, "Club" },
    { Item_CLUB_OF_LACONIUM, "CLUB OF LACONIUM" },
    { Item_MACE_OF_ADAMAN, "MACE OF ADAMAN" },
    { Item_CLUB_OF_ZUMIURAN, "CLUB OF ZUMIURAN" },
    { Item_Rod, "Rod" },
    { Item_Pole, "Pole" },
    { Item_Pillar, "Pillar" },
    { Item_Striker, "Striker" },
    { Item_BATTLE_VERGE, "BATTLE VERGE" },
    { Item_BRAVE_HAMMER, "BRAVE HAMMER" },
    { Item_ALIVE_AQHU, "ALIVE AQHU" },
    { Item_Wand, "Wand" },
    { Item_Staff, "Staff" },
    { Item_Baton, "Baton" },
    { Item_Scepter, "Scepter" },
    { Item_FIRE_SCEPTER_AGNI, "FIRE SCEPTER:AGNI" },
    { Item_ICE_STAFF_DAGON, "ICE STAFF:DAGON" },
    { Item_STORM_WAND_INDRA, "STORM WAND:INDRA" },
    { Item_PHOTON_CLAW, "PHOTON CLAW" },
    { Item_SILENCE_CLAW, "SILENCE CLAW" },
    { Item_NEIS_CLAW, "NEI'S CLAW" },
    { Item_DOUBLE_SABER, "DOUBLE SABER" },
    { Item_STAG_CUTLERY, "STAG CUTLERY" },
    { Item_TWIN_BRAND, "TWIN BRAND" },
    { Item_BRAVE_KNUCKLE, "BRAVE KNUCKLE" },
    { Item_ANGRY_FIST, "ANGRY FIST" },
    { Item_GOD_HAND, "GOD HAND" },
    { Item_SONIC_KNUCKLE, "SONIC KNUCKLE" },
    { Item_OROTIAGITO_alt, "OROTIAGITO (alt)" },
    { Item_OROTIAGITO, "OROTIAGITO" },
    { Item_AGITO_1975, "AGITO (AUW 1975)" },
    { Item_AGITO_1983, "AGITO (AUW 1983)" },
    { Item_AGITO_2001, "AGITO (AUW 2001)" },
    { Item_AGITO_1991, "AGITO (AUW 1991)" },
    { Item_AGITO_1977, "AGITO (AUW 1977)" },
    { Item_AGITO_1980, "AGITO (AUW 1980)" },
    { Item_SOUL_EATER, "SOUL EATER" },
    { Item_SOUL_BANISH, "SOUL BANISH" },
    { Item_SPREAD_NEEDLE, "SPREAD NEEDLE" },
    { Item_HOLY_RAY, "HOLY RAY" },
    { Item_INFERNO_BAZOOKA, "INFERNO BAZOOKA" },
    { Item_FLAME_VISIT, "FLAME VISIT" },
    { Item_AKIKOS_FRYING_PAN, "AKIKO'S FRYING PAN" },
    { Item_C_SORCERERS_CANE, "C-SORCERER'S CANE" },
    { Item_S_BEATS_BLADE, "S-BEAT'S BLADE" },
    { Item_P_ARMSS_BLADE, "P-ARMS'S BLADE" },
    { Item_DELSABERS_BUSTER, "DELSABER'S BUSTER" },
    { Item_C_BRINGERS_RIFLE, "C-BRINGER'S RIFLE" },
    { Item_EGG_BLASTER, "EGG BLASTER" },
    { Item_PSYCHO_WAND, "PSYCHO WAND" },
    { Item_HEAVEN_PUNISHER, "HEAVEN PUNISHER" },
    { Item_LAVIS_CANNON, "LAVIS CANNON" },
    { Item_VICTOR_AXE, "VICTOR AXE" },
    { Item_CHAIN_SAWD, "CHAIN SAWD" },
    { Item_CADUCEUS, "CADUCEUS" },
    { Item_STING_TIP, "STING TIP" },
    { Item_MAGICAL_PIECE, "MAGICAL PIECE" },
    { Item_TECHNICAL_CROZIER, "TECHNICAL CROZIER" },
    { Item_SUPPRESSED_GUN, "SUPPRESSED GUN" },
    { Item_ANCIENT_SABER, "ANCIENT SABER" },
    { Item_HARISEN_BATTLE_FAN, "HARISEN BATTLE FAN" },
    { Item_YAMIGARASU, "YAMIGARASU" },
    { Item_AKIKOS_WOK, "AKIKO'S WOK" },
    { Item_TOY_HAMMER, "TOY HAMMER" },
    { Item_ELYSION, "ELYSION" },
    { Item_RED_SABER, "RED SABER" },
    { Item_METEOR_CUDGEL, "METEOR CUDGEL" },
    { Item_MONKEY_KING_BAR, "MONKEY KING BAR" },
    { Item_DOUBLE_CANNON, "DOUBLE CANNON" },
    { Item_HUGE_BATTLE_FAN, "HUGE BATTLE FAN" },
    { Item_TSUMIKIRI_J_SWORD, "TSUMIKIRI J-SWORD" },
    { Item_SEALED_J_SWORD, "SEALED J-SWORD" },
    { Item_RED_SWORD, "RED SWORD" },
    { Item_CRAZY_TUNE, "CRAZY TUNE" },
    { Item_TWIN_CHAKRAM, "TWIN CHAKRAM" },
    { Item_WOK_OF_AKIKOS_SHOP, "WOK OF AKIKO'S SHOP" },
    { Item_LAVIS_BLADE, "LAVIS BLADE" },
    { Item_RED_DAGGER, "RED DAGGER" },
    { Item_MADAMS_PARASOL, "MADAM'S PARASOL" },
    { Item_MADAMS_UMBRELLA, "MADAM'S UMBRELLA" },
    { Item_IMPERIAL_PICK, "IMPERIAL PICK" },
    { Item_BERDYSH, "BERDYSH" },
    { Item_RED_PARTISAN, "RED PARTISAN" },
    { Item_FLIGHT_CUTTER, "FLIGHT CUTTER" },
    { Item_FLIGHT_FAN, "FLIGHT FAN" },
    { Item_RED_SLICER, "RED SLICER" },
    { Item_HANDGUN_GULD, "HANDGUN:GULD" },
    { Item_HANDGUN_MILLA, "HANDGUN:MILLA" },
    { Item_RED_HANDGUN, "RED HANDGUN" },
    { Item_FROZEN_SHOOTER, "FROZEN SHOOTER" },
    { Item_ANTI_ANDROID_RIFLE, "ANTI ANDROID RIFLE" },
    { Item_ROCKET_PUNCH, "ROCKET PUNCH" },
    { Item_SAMBA_MARACAS, "SAMBA MARACAS" },
    { Item_TWIN_PSYCHOGUN, "TWIN PSYCHOGUN" },
    { Item_DRILL_LAUNCHER, "DRILL LAUNCHER" },
    { Item_GULD_MILLA, "GULD MILLA" },
    { Item_RED_MECHGUN, "RED MECHGUN" },
    { Item_BERLA_CANNON, "BERLA CANNON" },
    { Item_PANZER_FAUST, "PANZER FAUST" },
    { Item_SUMMIT_MOON, "SUMMIT MOON" },
    { Item_WINDMILL, "WINDMILL" },
    { Item_EVIL_CURST, "EVIL CURST" },
    { Item_FLOWER_CANE, "FLOWER CANE" },
    { Item_HILDEBEARS_CANE, "HILDEBEAR'S CANE" },
    { Item_HILDEBLUES_CANE, "HILDEBLUE'S CANE" },
    { Item_RABBIT_WAND, "RABBIT WAND" },
    { Item_PLANTAIN_LEAF, "PLANTAIN LEAF" },
    { Item_DEMONIC_FORK, "DEMONIC FORK" },
    { Item_STRIKER_OF_CHAO, "STIRKER OF CHAO" },
    { Item_BROOM, "BROOM" },
    { Item_PROPHETS_OF_MOTAV, "PROPHETS OF MOTAV" },
    { Item_THE_SIGH_OF_A_GOD, "THE SIGH OF A GOD" },
    { Item_TWINKLE_STAR, "TWINKLE STAR" },
    { Item_PLANTAIN_FAN, "PLANTAIN FAN" },
    { Item_TWIN_BLAZE, "TWIN BLAZE" },
    { Item_MARINAS_BAG, "MARINA'S BAG" },
    { Item_DRAGONS_CLAW, "DRAGON'S CLAW" },
    { Item_PANTHERS_CLAW, "PANTHER'S CLAW" },
    { Item_S_REDS_BLADE, "S-RED'S BLADE" },
    { Item_PLANTAIN_HUGE_FAN, "PLANTAIN HUGE FAN" },
    { Item_CHAMELEON_SCYTHE, "CHAMELEON SCYTHE" },
    { Item_YASMINKOV_3000R, "YASMINKOV 3000R" },
    { Item_ANO_RIFLE, "ANO RIFLE" },
    { Item_BARANZ_LAUNCHER, "BARANZ LAUNCHER" },
    { Item_BRANCH_OF_PAKUPAKU, "BRANCH OF PAKUPAKU" },
    { Item_HEART_OF_POUMN, "HEART OF POUMN" },
    { Item_YASMINKOV_2000H, "YASMINKOV 2000H" },
    { Item_YASMINKOV_7000V, "YASMINKOV 7000V" },
    { Item_YASMINKOV_9200M, "YASMINKOV 9200M" },
    { Item_MASER_BEAM, "MASER BEAM" },
    { Item_GAME_MAGAZNE, "GAME MAGAZNE" },
    { Item_FLOWER_BOUQUET, "FLOWER BOUQUET" },
    { Item_SRANK_SABER, "SABER" },
    { Item_SRANK_SWORD, "SWORD" },
    { Item_SRANK_BLADE, "BLADE" },
    { Item_SRANK_PARTISAN, "PARTISAN" },
    { Item_SRANK_SLICER, "SLICER" },
    { Item_SRANK_GUN, "GUN" },
    { Item_SRANK_RIFLE, "RIFLE" },
    { Item_SRANK_MECHGUN, "MECHGUN" },
    { Item_SRANK_SHOT, "SHOT" },
    { Item_SRANK_CANE, "CANE" },
    { Item_SRANK_ROD, "ROD" },
    { Item_SRANK_WAND, "WAND" },
    { Item_SRANK_TWIN, "TWIN" },
    { Item_SRANK_CLAW, "CLAW" },
    { Item_SRANK_BAZOOKA, "BAZOOKA" },
    { Item_SRANK_NEEDLE, "NEEDLE" },
    { Item_SRANK_SCYTHE, "SCYTHE" },
    { Item_SRANK_HAMMER, "HAMMER" },
    { Item_SRANK_MOON, "MOON" },
    { Item_SRANK_PSYCHOGUN, "PSYCHOGUN" },
    { Item_SRANK_PUNCH, "PUNCH" },
    { Item_SRANK_WINDMILL, "WINDMILL" },
    { Item_SRANK_HARISEN, "HARISEN" },
    { Item_SRANK_J_BLADE, "J-BLADE" },
    { Item_SRANK_J_CUTTER, "J-CUTTER" },
    { Item_Frame, "Frame" },
    { Item_Armor, "Armor" },
    { Item_Psy_Armor, "Psy Armor" },
    { Item_Giga_Frame, "Giga Frame" },
    { Item_Soul_Frame, "Soul Frame" },
    { Item_Cross_Armor, "Cross Armor" },
    { Item_Solid_Frame, "Solid Frame" },
    { Item_Brave_Armor, "Brace Armor" },
    { Item_Hyper_Frame, "Hyper Frame" },
    { Item_Grand_Armor, "Grand Armor" },
    { Item_Shock_Frame, "Shock Frame" },
    { Item_Kings_Frame, "King's Frame" },
    { Item_Dragon_Frame, "Dragon Frame" },
    { Item_Absorb_Armor, "Absorb Armor" },
    { Item_Protect_Frame, "Protect Frame" },
    { Item_General_Armor, "General Armor" },
    { Item_Perfect_Frame, "Perfect Frame" },
    { Item_Valiant_Frame, "Valiant Frame" },
    { Item_Imperial_Armor, "Imperial Armor" },
    { Item_Holiness_Armor, "Holiness Armor" },
    { Item_Guardian_Armor, "Guardian Armor" },
    { Item_Divinity_Armor, "Divinity Armor" },
    { Item_Ultimate_Frame, "Ultimate Frame" },
    { Item_Celestial_Armor, "Celestial Armor" },
    { Item_HUNTER_FIELD, "HUNTER FIELD" },
    { Item_RANGER_FIELD, "RANGER FIELD" },
    { Item_FORCE_FIELD, "FORCE FIELD" },
    { Item_REVIVAL_GARMENT, "REVIVAL GARMENT" },
    { Item_SPIRIT_GARMENT, "SPIRIT GARMENT" },
    { Item_STINK_FRAME, "STINK FRAME" },
    { Item_D_PARTS_ver1_01, "D-PARTS ver1.01" },
    { Item_D_PARTS_ver2_10, "D-PARTS ver2.10" },
    { Item_PARASITE_WEAR_De_Rol, "PARASITE WEAR:De Rol" },
    { Item_PARASITE_WEAR_Nelgal, "PARASITE WEAR:Nelgal" },
    { Item_PARASITE_WEAR_Vajulla, "PARASITE WEAR:Vajulla" },
    { Item_SENSE_PLATE, "SENSE PLATE" },
    { Item_GRAVITON_PLATE, "GRAVITON PLATE" },
    { Item_ATTRIBUTE_PLATE, "ATTRIBUTE PLATE" },
    { Item_FLOWENS_FRAME, "FLOWEN'S FRAME" },
    { Item_CUSTOM_FRAME_ver_OO, "CUSTOM FRAME ver.OO" },
    { Item_DBS_ARMOR, "DB'S ARMOR" },
    { Item_GUARD_WAVE, "GUARD WAVE" },
    { Item_DF_FIELD, "DF FIELD" },
    { Item_LUMINOUS_FIELD, "LUMINOUS FIELD" },
    { Item_CHU_CHU_FEVER, "CHU CHU FEVER" },
    { Item_LOVE_HEART, "LOVE HEART" },
    { Item_FLAME_GARMENT, "FLAME GARMENT" },
    { Item_VIRUS_ARMOR_Lafuteria, "VIRUS ARMOR:Lafuteria" },
    { Item_BRIGHTNESS_CIRCLE, "BRIGHTNESS CIRCLE" },
    { Item_AURA_FIELD, "AURA FIELD" },
    { Item_ELECTRO_FRAME, "ELECTRO FRAME" },
    { Item_SACRED_CLOTH, "SACRED CLOTH" },
    { Item_SMOKING_PLATE, "SMOKING PLATE" },
    { Item_Barrier, "Barrier" },
    { Item_Shield, "Shield" },
    { Item_Core_Shield, "Core Shield" },
    { Item_Giga_Shield, "Giga Shield" },
    { Item_Soul_Barrier, "Soul Barrier" },
    { Item_Hard_Shield, "Hard Shield" },
    { Item_Brave_Barrier, "Brave Barrier" },
    { Item_Solid_Shield, "Solid Shield" },
    { Item_Flame_Barrier, "Flame Barrier" },
    { Item_Plasma_Barrier, "Plasma Barrier" },
    { Item_Freeze_Barrier, "Freeze Barrier" },
    { Item_Psychic_Barrier, "Psychic Barrier" },
    { Item_General_Shield, "General Shield" },
    { Item_Protect_Barrier, "Protect Barrier" },
    { Item_Glorious_Shield, "Glorious Shield" },
    { Item_Imperial_Barrier, "Imperial Barrier" },
    { Item_Guardian_Shield, "Guardian Shield" },
    { Item_Divinity_Barrier, "Divinity Barrier" },
    { Item_Ultimate_Shield, "Ultimate Shield" },
    { Item_Spiritual_Shield, "Spiritual Shield" },
    { Item_Celestial_Shield, "Celestial Shield" },
    { Item_INVISIBLE_GUARD, "INVISIBLE GUARD" },
    { Item_SACRED_GUARD, "SACRED GUARD" },
    { Item_S_PARTS_ver1_16, "S-PARTS ver1.16" },
    { Item_S_PARTS_ver2_01, "S-PARTS ver2.01" },
    { Item_LIGHT_RELIEF, "LIGHT RELIEF" },
    { Item_SHIELD_OF_DELSABER, "SHIELD OF DELSABER" },
    { Item_FORCE_WALL, "FORCE WALL" },
    { Item_RANGER_WALL, "RANGER WALL" },
    { Item_HUNTER_WALL, "HUNTER WALL" },
    { Item_ATTRIBUTE_WALL, "ATTRIBUTE WALL" },
    { Item_SECRET_GEAR, "SECRET GEAR" },
    { Item_COMBAT_GEAR, "COMBAT GEAR" },
    { Item_PROTO_REGENE_GEAR, "PROTO REGENE GEAR" },
    { Item_REGENERATE_GEAR, "REGENERATE GEAR" },
    { Item_REGENE_GEAR_ADV, "REGENE GEAR ADV" },
    { Item_FLOWENS_SHIELD, "FLOWEN'S SHIELD" },
    { Item_CUSTOM_BARRIER_ver_OO, "CUSTOM BARRIER ver.OO" },
    { Item_DBS_SHIELD, "DB'S SHIELD" },
    { Item_RED_RING, "RED RING" },
    { Item_TRIPOLIC_SHIELD, "TRIPOLIC SHIELD" },
    { Item_STANDSTILL_SHIELD, "STANDSTILL SHIELD" },
    { Item_SAFETY_HEART, "SAFETY HEART" },
    { Item_KASAMI_BRACER, "KASAMI BRACER" },
    { Item_GODS_SHIELD_SUZAKU, "GODS SHIELD SUZAKU" },
    { Item_GODS_SHIELD_GENBU, "GODS SHIELD GENBU" },
    { Item_GODS_SHIELD_BYAKKO, "GODS SHIELD BYAKKO" },
    { Item_GODS_SHIELD_SEIRYU, "GODS SHIELD SEIRYU" },
    { Item_HANTERS_SHELL, "HANTER'S SHELL" },
    { Item_RIKOS_GLASSES, "RIKO'S GLASSES" },
    { Item_RIKOS_EARRING, "RIKO'S EARRING" },
    { Item_BLUE_RING, "BLUE RING" },
    { Item_YELLOW_RING, "YELLOW RING" },
    { Item_SECURE_FEET, "SECURE FEET" },
    { Item_PURPLE_RING, "PURPLE RING" },
    { Item_GREEN_RING, "GREEN RING" },
    { Item_BLACK_RING, "BLACK RING" },
    { Item_WHITE_RING, "WHITE RING" },
    { Item_Knight_Power, "Knight/Power" },
    { Item_General_Power, "General/Power" },
    { Item_Ogre_Power, "Ogre/Power" },
    { Item_God_Power, "God/Power" },
    { Item_Priest_Mind, "Priest/Mind" },
    { Item_General_Mind, "General/Mind" },
    { Item_Angel_Mind, "Angel/Mind" },
    { Item_God_Mind, "God/Mind" },
    { Item_Marksman_Arm, "Marksman/Arm" },
    { Item_General_Arm, "General/Arm" },
    { Item_Elf_Arm, "Elf/Arm" },
    { Item_God_Arm, "God/Arm" },
    { Item_Thief_Legs, "Thief/Legs" },
    { Item_General_Legs, "General/Legs" },
    { Item_Elf_Legs, "Elf/Legs" },
    { Item_God_Legs, "God/Legs" },
    { Item_Digger_HP, "Digger/HP" },
    { Item_General_HP, "General/HP" },
    { Item_Dragon_HP, "Dragon/HP" },
    { Item_God_HP, "God/HP" },
    { Item_Magician_TP, "Magician/TP" },
    { Item_General_TP, "General/TP" },
    { Item_Angel_TP, "Angel/TP" },
    { Item_God_TP, "God/TP" },
    { Item_Warrior_Body, "Warrior/Body" },
    { Item_General_Body, "General/Body" },
    { Item_Metal_Body, "Metal/Body" },
    { Item_God_Body, "God/Body", },
    { Item_Angel_Luck, "Angel/Luck" },
    { Item_God_Luck, "God/Luck" },
    { Item_Master_Ability, "Master/Ability" },
    { Item_Hero_Ability, "Hero/Ability" },
    { Item_God_Ability, "God/Ability" },
    { Item_Resist_Fire, "Resist/Fire" },
    { Item_Resist_Flame, "Resist/Flame" },
    { Item_Resist_Burning, "Resist/Burning" },
    { Item_Resist_Cold, "Resist/Cold" },
    { Item_Resist_Freeze, "Resist/Freeze" },
    { Item_Resist_Blizzard, "Resist/Blizzard" },
    { Item_Resist_Shock, "Resist/Shock" },
    { Item_Resist_Thunder, "Resist/Thunder" },
    { Item_Resist_Storm, "Resist/Storm" },
    { Item_Resist_Light, "Resist/Light" },
    { Item_Resist_Saint, "Resist/Saint" },
    { Item_Resist_Holy, "Resist/Holy" },
    { Item_Resist_Dark, "Resist/Dark" },
    { Item_Resist_Evil, "Resist/Evil" },
    { Item_Resist_Devil, "Resist/Devil" },
    { Item_All_Resist, "All/Resist" },
    { Item_Super_Resist, "Super/Resist" },
    { Item_Perfect_Resist, "Perfect/Resist" },
    { Item_HP_Restorate, "HP/Restorate" },
    { Item_HP_Generate, "HP/Generate" },
    { Item_HP_Revival, "HP/Revival" },
    { Item_TP_Restorate, "TP/Restorate" },
    { Item_TP_Generate, "TP/Generate" },
    { Item_TP_Revival, "TP/Revival" },
    { Item_PB_Amplifier, "PB/Amplifier" },
    { Item_PB_Generate, "PB/Generate" },
    { Item_PB_Create, "PB/Create" },
    { Item_Wizard_Technique, "Wizard/Technique" },
    { Item_Devil_Technique, "Devil/Technique" },
    { Item_God_Technique, "God/Technique" },
    { Item_General_Battle, "General/Battle" },
    { Item_Devil_Battle, "Devil/Battle" },
    { Item_God_Battle, "God/Battle" },
    { Item_State_Maintenance, "State/Maintenance" },
    { Item_Trap_Search, "Trap/Search" },
    { Item_Mag, "Mag" },
    { Item_Varuna, "Varuna" },
    { Item_Mitra, "Mitra" },
    { Item_Surya, "Surya" },
    { Item_Vayu, "Vayu" },
    { Item_Varaha, "Varaha" },
    { Item_Kama, "Kama" },
    { Item_Ushasu, "Ushasu" },
    { Item_Apsaras, "Apsaras" },
    { Item_Kumara, "Kumara" },
    { Item_Kaitabha, "Kaitabha" },
    { Item_Tapas, "Tapas" },
    { Item_Bhirava, "Bhirava" },
    { Item_Kalki, "Kalki" },
    { Item_Rudra, "Rudra" },
    { Item_Marutah, "Marutah" },
    { Item_Yaksa, "Yaksa" },
    { Item_Sita, "Sita" },
    { Item_Garuda, "Garuda" },
    { Item_Nandin, "Nandin" },
    { Item_Ashvinau, "Ashvinau" },
    { Item_Ribhava, "Ribhava" },
    { Item_Soma, "Soma" },
    { Item_Ila, "Ila" },
    { Item_Durga, "Durga" },
    { Item_Vritra, "Vritra" },
    { Item_Namuci, "Namuci" },
    { Item_Sumba, "Sumba" },
    { Item_Naga, "Naga" },
    { Item_Pitri, "Pitri" },
    { Item_Kabanda, "Kabanda" },
    { Item_Ravana, "Ravana" },
    { Item_Marica, "Marica" },
    { Item_Soniti, "Soniti" },
    { Item_Preta, "Preta" },
    { Item_Andhaka, "Andhaka" },
    { Item_Bana, "Bana" },
    { Item_Naraka, "Naraka" },
    { Item_Madhu, "Madhu" },
    { Item_Churel, "Churel" },
    { Item_ROBOCHAO, "ROBOCHAO" },
    { Item_OPA_OPA, "OPA-OPA" },
    { Item_PIAN, "PIAN" },
    { Item_CHAO, "CHAO" },
    { Item_CHU_CHU, "CHU CHU" },
    { Item_KAPU_KAPU, "KAPU KAPU" },
    { Item_ANGELS_WING, "ANGEL'S WING" },
    { Item_DEVILS_WING, "DEVIL'S WING" },
    { Item_ELENOR, "ELENOR" },
    { Item_MARK3, "MARK3" },
    { Item_MASTER_SYSTEM, "MASTER SYSTEM" },
    { Item_GENESIS, "GENESIS" },
    { Item_SEGA_SATURN, "SEGA SATURN" },
    { Item_DREAMCAST, "DREAMCAST" },
    { Item_HAMBURGER, "HAMBURGER" },
    { Item_PANZERS_TAIL, "PANZER'S TAIL" },
    { Item_DAVILS_TAIL, "DAVIL'S TAIL" },
    { Item_Monomate, "Monomate" },
    { Item_Dimate, "Dimate" },
    { Item_Trimate, "Trimate" },
    { Item_Monofluid, "Monofluid" },
    { Item_Difluid, "Difluid" },
    { Item_Trifluid, "Trifluid" },
    { Item_Disk_Lv01, "Disk:Lv.1" },
    { Item_Disk_Lv02, "Disk:Lv.2" },
    { Item_Disk_Lv03, "Disk:Lv.3" },
    { Item_Disk_Lv04, "Disk:Lv.4" },
    { Item_Disk_Lv05, "Disk:Lv.5" },
    { Item_Disk_Lv06, "Disk:Lv.6" },
    { Item_Disk_Lv07, "Disk:Lv.7" },
    { Item_Disk_Lv08, "Disk:Lv.8" },
    { Item_Disk_Lv09, "Disk:Lv.9" },
    { Item_Disk_Lv10, "Disk:Lv.10" },
    { Item_Disk_Lv11, "Disk:Lv.11" },
    { Item_Disk_Lv12, "Disk:Lv.12" },
    { Item_Disk_Lv13, "Disk:Lv.13" },
    { Item_Disk_Lv14, "Disk:Lv.14" },
    { Item_Disk_Lv15, "Disk:Lv.15" },
    { Item_Disk_Lv16, "Disk:Lv.16" },
    { Item_Disk_Lv17, "Disk:Lv.17" },
    { Item_Disk_Lv18, "Disk:Lv.18" },
    { Item_Disk_Lv19, "Disk:Lv.19" },
    { Item_Disk_Lv20, "Disk:Lv.20" },
    { Item_Disk_Lv21, "Disk:Lv.21" },
    { Item_Disk_Lv22, "Disk:Lv.22" },
    { Item_Disk_Lv23, "Disk:Lv.23" },
    { Item_Disk_Lv24, "Disk:Lv.24" },
    { Item_Disk_Lv25, "Disk:Lv.25" },
    { Item_Disk_Lv26, "Disk:Lv.26" },
    { Item_Disk_Lv27, "Disk:Lv.27" },
    { Item_Disk_Lv28, "Disk:Lv.28" },
    { Item_Disk_Lv29, "Disk:Lv.29" },
    { Item_Disk_Lv30, "Disk:Lv.30" },
    { Item_Sol_Atomizer, "Sol Atomizer" },
    { Item_Moon_Atomizer, "Moon Atomizer" },
    { Item_Star_Atomizer, "Star Atomizer" },
    { Item_Antidote, "Antidote" },
    { Item_Antiparalysis, "Antiparalysis" },
    { Item_Telepipe, "Telepipe" },
    { Item_Trap_Vision, "Trap Vision" },
    { Item_Scape_Doll, "Scape Doll" },
    { Item_Monogrinder, "Monogrinder" },
    { Item_Digrinder, "Digrinder" },
    { Item_Trigrinder, "Trigrinder" },
    { Item_Power_Material, "Power Material" },
    { Item_Mind_Material, "Mind Material" },
    { Item_Evade_Material, "Evade Material" },
    { Item_HP_Material, "HP Material" },
    { Item_TP_Material, "TP Material" },
    { Item_Def_Material, "Def Material" },
    { Item_Hit_Material, "Hit Material" },
    { Item_Luck_Material, "Luck Material" },
    { Item_Cell_of_MAG_502, "Cell of MAG 502" },
    { Item_Cell_of_MAG_213, "Cell of MAG 213" },
    { Item_Parts_of_RoboChao, "Parts of RoboChao" },
    { Item_Heart_of_Opa_Opa, "Heart of Opa Opa" },
    { Item_Heart_of_Pian, "Heart of Pian" },
    { Item_Heart_of_Chao, "Heart of Chao" },
    { Item_Sorcerers_Right_Arm, "Sorcerer's Right Arm" },
    { Item_S_beats_Arms, "S-beat's Arms" },
    { Item_P_arms_Arms, "P-arm's Arms" },
    { Item_Delsabers_Right_Arm, "Delsaber's Right Arm" },
    { Item_C_bringers_Right_Arm, "C-bringer's Right Arm" },
    { Item_Delsabres_Left_Arm, "Delsabre's Left Arm" },
    { Item_Book_of_KATANA1, "Book of KATANA1" },
    { Item_Book_of_KATANA2, "Book of KATANA2" },
    { Item_Book_of_KATANA3, "Book of KATANA3" },
    { Item_S_reds_Arms, "S-red's Arms" },
    { Item_Dragons_Claw, "Dragon's Claw" },
    { Item_Hildebears_Head, "Hildebear's Head" },
    { Item_Hildeblues_Head, "Hildeblue's Head" },
    { Item_Parts_of_Baranz, "Parts of Baranz" },
    { Item_Belras_Right_Arm, "Belra's Right Arm" },
    { Item_Joint_Parts, "Joint Parts" },
    { Item_Weapons_Bronze_Badge, "Weapons Bronze Badge" },
    { Item_Weapons_Silver_Badge, "Weapons Silver Badge" },
    { Item_Weapons_Gold_Badge, "Weapons Gold Badge" },
    { Item_Weapons_Crystal_Badge, "Weapons Crystal Badge" },
    { Item_Weapons_Steel_Badge, "Weapons Steel Badge" },
    { Item_Weapons_Aluminum_Badge, "Weapons Aluminum Badge" },
    { Item_Weapons_Leather_Badge, "Weapons Leather Badge" },
    { Item_Weapons_Bone_Badge, "Weapons Bone Badge" },
    { Item_Letter_of_appreciation, "Letter of appreciation" },
    { Item_Autograph_Album, "Autograph Album" },
    { Item_High_level_Mag_Cell_Eno, "High-level Mag Cell, Eno" },
    { Item_High_level_Mag_Armor_Uru, "High-level Mag Armor, Uru" },
    { Item_Special_Gene_Flou, "Special Gene Flou" },
    { Item_Sound_Source_FM, "Sound Source FM" },
    { Item_Parts_of_68000, "Parts of \"68000\"" },
    { Item_SH2, "SH2" },
    { Item_SH4, "SH4" },
    { Item_Modem, "Modem" },
    { Item_Power_VR, "Power VR" },
    { Item_Glory_in_the_past, "Glory in the past" },
    { Item_Valentines_Chocolate, "Valentine's Chocolate" },
    { Item_New_Years_Card, "New Year's Card" },
    { Item_Christmas_Card, "Christmas Card" },
    { Item_Birthday_Card, "Birthday Card" },
    { Item_Proof_of_Sonic_Team, "Proof of Sonic Team" },
    { Item_Special_Event_Ticket, "Special Event Ticket" },
    { Item_Flower_Bouquet, "Flower Bouquet" },
    { Item_Cake, "Cake" },
    { Item_Accessories, "Accessories" },
    { Item_Mr_Nakas_Business_Card, "Mr.Naka's Business Card" },
    { Item_NoSuchItem, "" }
};

const char *item_get_name_by_code(item_code_t code) {
    item_map_t *cur = &item_list[0];

    /* Take care of mags so that we'll match them properly... */
    if((code & 0xFF) == 0x02) {
        code &= 0xFFFF;
    }

    /* Look through the list for the one we want */
    while(cur->code != Item_NoSuchItem) {
        if(cur->code == code) {
            return cur->name;
        }

        ++cur;
    }

    /* No item found... */
    return NULL;
}

const char *item_get_name(item_t *item) {
    uint32_t code = item->data_b[0] | (item->data_b[1] << 8) |
        (item->data_b[2] << 16);

    /* Make sure we take care of any v2 item codes */
    switch(item->data_b[0]) {
        case 0x00:  /* Weapon */
            if(item->data_b[5]) {
                code = (item->data_b[5] << 8);
            }
            break;

        case 0x01:  /* Guard */
            if(item->data_b[1] != 0x03 && item->data_b[3]) {
                code = code | (item->data_b[3] << 16);
            }
            break;

        case 0x02:  /* Mag */
            if(item->data_b[1] == 0x00 && item->data_b[2] >= 0xC9) {
                code = 0x02 | (((item->data_b[2] - 0xC9) + 0x2C) << 8);
            }
            break;

        case 0x03: /* Tool */
            if(code == 0x060D03 && item->data_b[3]) {
                code = 0x000E03 | ((item->data_b[3] - 1) << 16);
            }
            break;
    }

    return item_get_name_by_code((item_code_t)code);
}

int item_remove_from_inv(item_t *inv, int inv_count, uint32_t item_id,
                         uint32_t amt) {
    int i;
    uint32_t tmp;

    /* Look for the item in question */
    for(i = 0; i < inv_count; ++i) {
        if(inv[i].item_id == item_id) {
            break;
        }
    }

    /* Did we find it? If not, return error. */
    if(i == inv_count) {
        return -1;
    }

    /* Check if the item is stackable, since we may have to do some stuff
       differently... */
    if(item_is_stackable(LE32(inv[i].data_l[0])) && amt != 0xFFFFFFFF) {
        tmp = inv[i].data_b[5];

        if(amt < tmp) {
            tmp -= amt;
            inv[i].data_b[5] = tmp;
            return 0;
        }
    }

    /* Move the rest of the items down to take over the place that the item in
       question used to occupy. */
    memmove(inv + i, inv + i + 1, (inv_count - i - 1) * sizeof(item_t));
    return 1;
}

int item_add_to_inv(item_t *inv, int inv_count, item_t *it) {
    int i;

    /* Make sure there's space first. */
    if(inv_count == 30) {
        return -1;
    }

    /* Look for the item in question. If it exists, we're in trouble! */
    for(i = 0; i < inv_count; ++i) {
        if(inv[i].item_id == it->item_id) {
            return -1;
        }
    }
    
    /* Check if the item is stackable, since we may have to do some stuff
       differently... */
    if(item_is_stackable(LE32(it->data_l[0]))) {
        /* Look for anything that matches this item in the inventory. */
        for(i = 0; i < inv_count; ++i) {
            if(inv[i].data_l[0] == it->data_l[0]) {
                inv[i].data_b[5] += it->data_b[5];
                return 0;
            }
        }
    }

    /* Copy the new item in at the end. */
    inv[inv_count] = *it;
    inv[inv_count].equipped = inv[inv_count].tech = 0;
    inv[inv_count].flags = 0;
    return 1;
}

void cleanup_bb_bank(ship_client_t *c) {
    uint32_t item_id = 0x80010000 | (c->client_id << 21);
    uint32_t count = LE32(c->bb_pl->bank.item_count), i;

    for(i = 0; i < count; ++i) {
        c->bb_pl->bank.items[i].item_id = LE32(item_id);
        ++item_id;
    }

    /* Clear all the rest of them... */
    for(; i < 200; ++i) {
        memset(&c->bb_pl->bank.items[i], 0, sizeof(sylverant_bitem_t));
        c->bb_pl->bank.items[i].item_id = 0xFFFFFFFF;
    }
}

int item_deposit_to_bank(ship_client_t *c, sylverant_bitem_t *it) {
    uint32_t i, count = LE32(c->bb_pl->bank.item_count);
    int amount;

    /* Make sure there's space first. */
    if(count == 200) {
        return -1;
    }

    /* Check if the item is stackable, since we may have to do some stuff
       differently... */
    if(item_is_stackable(LE32(it->data_l[0]))) {
        /* Look for anything that matches this item in the inventory. */
        for(i = 0; i < count; ++i) {
            if(c->bb_pl->bank.items[i].data_l[0] == it->data_l[0]) {
                amount = c->bb_pl->bank.items[i].data_b[5] += it->data_b[5];
                c->bb_pl->bank.items[i].amount = LE16(amount);
                return 0;
            }
        }
    }

    /* Copy the new item in at the end. */
    c->bb_pl->bank.items[count] = *it;
    ++count;
    c->bb_pl->bank.item_count = count;

    return 1;
}

int item_take_from_bank(ship_client_t *c, uint32_t item_id, uint8_t amt,
                        sylverant_bitem_t *rv) {
    uint32_t i, count = LE32(c->bb_pl->bank.item_count);
    sylverant_bitem_t *it;

    /* Look for the item in question */
    for(i = 0; i < count; ++i) {
        if(c->bb_pl->bank.items[i].item_id == item_id) {
            break;
        }
    }

    /* Did we find it? If not, return error. */
    if(i == count) {
        return -1;
    }

    /* Grab the item in question, and copy the data to the return pointer. */
    it = &c->bb_pl->bank.items[i];
    *rv = *it;

    /* Check if the item is stackable, since we may have to do some stuff
       differently... */
    if(item_is_stackable(LE32(it->data_l[0]))) {
        if(amt < it->data_b[5]) {
            it->data_b[5] -= amt;
            it->amount = LE16(it->data_b[5]);

            /* Fix the amount on the returned value, and return. */
            rv->data_b[5] = amt;
            rv->amount = LE16(amt);

            return 0;
        }
        else if(amt > it->data_b[5]) {
            return -1;
        }
    }

    /* Move the rest of the items down to take over the place that the item in
       question used to occupy. */
    memmove(c->bb_pl->bank.items + i, c->bb_pl->bank.items + i + 1,
            (count - i - 1) * sizeof(sylverant_bitem_t));
    --count;
    c->bb_pl->bank.item_count = LE32(count);

    return 1;
}

int item_is_stackable(uint32_t code) {
    if((code & 0x000000FF) == 0x03) {
        code = (code >> 8) & 0xFF;

        if(code < 0x09 && code != 0x02) {
            return 1;
        }
    }

    return 0;
}
