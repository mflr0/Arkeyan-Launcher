#include "launcher/discord_presence.h"
#include <algorithm>
#include <array>
#include <utility>

// The pictures Discord shows, by address: Discord fetches them itself, the launcher ships none.
// The characters: the main picture of each character's page on the Skylanders Fandom wiki, matched by name
// (docs/settings.md, `[discord]`; credits in THIRD_PARTY_NOTICES.md).
namespace discordpresence
{
    namespace
    {

        // Sorted by toy type.
        constexpr std::array<std::pair<uint16_t, std::string_view>, 204> FigureImages{ {
            { 0, "https://static.wikia.nocookie.net/skylanders/images/e/e0/Whirlwind.png/revision/"
                 "latest?cb=20230209030741" }, // Whirlwind
            { 1, "https://static.wikia.nocookie.net/skylanders/images/d/d8/Sonic_Boom_shot.jpg/revision/"
                 "latest?cb=20111229144309" }, // Sonic Boom
            { 2, "https://static.wikia.nocookie.net/skylanders/images/5/50/290px-Warnado_03.jpg/revision/"
                 "latest?cb=20170326190641" }, // Warnado
            { 3, "https://static.wikia.nocookie.net/skylanders/images/6/65/LightningRod_01.jpg/revision/"
                 "latest?cb=20120408195548" }, // Lightning Rod
            { 4, "https://static.wikia.nocookie.net/skylanders/images/9/9c/Bash.jpg/revision/"
                 "latest?cb=20120701192307" }, // Bash
            { 5, "https://static.wikia.nocookie.net/skylanders/images/b/ba/Terrafin.jpeg/revision/"
                 "latest?cb=20200509230216" }, // Terrafin
            { 6, "https://static.wikia.nocookie.net/skylanders/images/3/32/Dino-Rang.jpg/revision/"
                 "latest?cb=20111113010216" }, // Dino-Rang
            { 7, "https://static.wikia.nocookie.net/skylanders/images/d/da/Prism_Break.jpg/revision/"
                 "latest?cb=20130716165215" }, // Prism Break
            { 8, "https://static.wikia.nocookie.net/skylanders/images/e/e9/310px-Sunburn_01.jpg/revision/"
                 "latest?cb=20170326190741" }, // Sunburn
            { 9, "https://static.wikia.nocookie.net/skylanders/images/6/65/Erup3.jpg/revision/"
                 "latest?cb=20120105061926" }, // Eruptor
            { 10, "https://static.wikia.nocookie.net/skylanders/images/9/9f/270px-Sky_ignitor.jpg/revision/"
                  "latest?cb=20170326183734" }, // Ignitor
            { 11, "https://static.wikia.nocookie.net/skylanders/images/0/06/Sky_flameslinger.jpg/revision/"
                  "latest?cb=20120105062024" }, // Flameslinger
            { 12, "https://static.wikia.nocookie.net/skylanders/images/7/7c/288px-Zap_Promo.jpg/revision/"
                  "latest?cb=20170326190258" }, // Zap
            { 13, "https://static.wikia.nocookie.net/skylanders/images/e/ee/300px-Wham-Shell_03.jpg/revision/"
                  "latest?cb=20170326190042" }, // Wham-Shell
            { 14, "https://static.wikia.nocookie.net/skylanders/images/6/65/Gill_Grunt_Promo.jpg/revision/"
                  "latest?cb=20120221180930" }, // Gill Grunt
            { 15, "https://static.wikia.nocookie.net/skylanders/images/f/f2/300px-SlamBam_01.jpg/revision/"
                  "latest?cb=20170326190151" }, // Slam Bam
            { 16, "https://static.wikia.nocookie.net/skylanders/images/7/73/Spyro.jpg/revision/"
                  "latest?cb=20120508052641" }, // Spyro
            { 17, "https://static.wikia.nocookie.net/skylanders/images/3/38/Voodood_Promo.jpg/revision/"
                  "latest?cb=20111215014110" }, // Voodood
            { 18, "https://static.wikia.nocookie.net/skylanders/images/3/33/300px-Double-Trouble_03.jpg/revision/"
                  "latest?cb=20170305211339" }, // Double Trouble
            { 19, "https://static.wikia.nocookie.net/skylanders/images/1/1f/Sky_trigger2.jpg/revision/"
                  "latest?cb=20120224061342" }, // Trigger Happy
            { 20, "https://static.wikia.nocookie.net/skylanders/images/7/7c/Tht.jpg/revision/"
                  "latest?cb=20111229144706" }, // Drobot
            { 21, "https://static.wikia.nocookie.net/skylanders/images/f/f9/290px-Drill-Sergeant_03.jpg/revision/"
                  "latest?cb=20170326183948" }, // Drill Sergeant
            { 22, "https://static.wikia.nocookie.net/skylanders/images/c/c4/290px-Boomer_Promo.jpg/revision/"
                  "latest?cb=20170326191733" }, // Boomer
            { 23, "https://static.wikia.nocookie.net/skylanders/images/5/55/Wrecking_Ball.jpg/revision/"
                  "latest?cb=20170326190841" }, // Wrecking Ball
            { 24, "https://static.wikia.nocookie.net/skylanders/images/0/05/Camo_02.jpg/revision/"
                  "latest?cb=20121015194842" }, // Camo
            { 25, "https://static.wikia.nocookie.net/skylanders/images/5/54/442px-Zook.jpg/revision/"
                  "latest?cb=20120307044638" }, // Zook
            { 26, "https://static.wikia.nocookie.net/skylanders/images/b/bc/Stealth_Elf_Promo.jpg/revision/"
                  "latest?cb=20120221220626" }, // Stealth Elf
            { 27, "https://static.wikia.nocookie.net/skylanders/images/c/ce/Stump_Smash_Promo.jpg/revision/"
                  "latest?cb=20120718090304" }, // Stump Smash
            { 28, "https://static.wikia.nocookie.net/skylanders/images/2/24/300px-Dark-Spyro_01.jpg/revision/"
                  "latest?cb=20170326190423" }, // Dark Spyro
            { 29, "https://static.wikia.nocookie.net/skylanders/images/4/48/290px-Hex.jpg/revision/"
                  "latest?cb=20170326190536" }, // Hex
            { 30, "https://static.wikia.nocookie.net/skylanders/images/5/5f/Chop_Chop_Promo.jpg/revision/"
                  "latest?cb=20120221181501" }, // Chop Chop
            { 31, "https://static.wikia.nocookie.net/skylanders/images/1/1d/Ghost_Roaster.jpg/revision/"
                  "latest?cb=20120921212240" }, // Ghost Roaster
            { 32, "https://static.wikia.nocookie.net/skylanders/images/2/27/Cynder_Promo.jpg/revision/"
                  "latest?cb=20140620184749" }, // Cynder
            { 100, "https://static.wikia.nocookie.net/skylanders/images/f/fb/Jet_Vac_Promo.jpg/revision/"
                   "latest?cb=20120605011208" }, // Jet-Vac
            { 101, "https://static.wikia.nocookie.net/skylanders/images/b/be/Swarm.jpg/revision/"
                   "latest?cb=20130918115129" }, // Swarm
            { 102, "https://static.wikia.nocookie.net/skylanders/images/d/d7/Crusher.jpg/revision/"
                   "latest?cb=20120911222242" }, // Crusher
            { 103, "https://static.wikia.nocookie.net/skylanders/images/4/4f/Earth_Flashwing.jpg/revision/"
                   "latest?cb=20121117065259" }, // Flashwing
            { 104, "https://static.wikia.nocookie.net/skylanders/images/b/b2/Hot_Head_Promo.jpg/revision/"
                   "latest?cb=20121117065411" }, // Hot Head
            { 105, "https://static.wikia.nocookie.net/skylanders/images/1/1d/Hot_Dog_Promo.jpg/revision/"
                   "latest?cb=20130130185918" }, // Hot Dog
            { 106, "https://static.wikia.nocookie.net/skylanders/images/b/bd/Skylanders-chill-top630.jpg/revision/"
                   "latest?cb=20120814223624" }, // Chill
            { 107, "https://static.wikia.nocookie.net/skylanders/images/3/36/Thumpback_-_.jpg/revision/"
                   "latest?cb=20130220165740" }, // Thumpback
            { 108, "https://static.wikia.nocookie.net/skylanders/images/7/7b/PopFizz.jpg/revision/"
                   "latest?cb=20121025125602" }, // Pop Fizz
            { 109, "https://static.wikia.nocookie.net/skylanders/images/7/78/Ninjini_Promo.jpg/revision/"
                   "latest?cb=20130310050701" }, // Ninjini
            { 110, "https://static.wikia.nocookie.net/skylanders/images/e/ec/SG_Illus_Bouncer_Crop_LoRes1.jpg/revision/"
                   "latest?cb=20120930202737" }, // Bouncer
            { 111, "https://static.wikia.nocookie.net/skylanders/images/1/1b/Sprocket_Promo.jpg/revision/"
                   "latest?cb=20121102050146" }, // Sprocket
            { 112, "https://static.wikia.nocookie.net/skylanders/images/e/e6/Tree_Rex_Artwork.jpg/revision/"
                   "latest?cb=20120604215621" }, // Tree Rex
            { 113, "https://static.wikia.nocookie.net/skylanders/images/4/41/Screenshot_48056.jpg/revision/"
                   "latest?cb=20250201015942" }, // Shroomboom
            { 114, "https://static.wikia.nocookie.net/skylanders/images/b/bd/Eye-Brawl_Promo.jpg/revision/"
                   "latest?cb=20130118221730" }, // Eye-Brawl
            { 115, "https://static.wikia.nocookie.net/skylanders/images/e/e6/Fright_Rider_Promo.jpg/revision/"
                   "latest?cb=20121102050400" }, // Fright Rider
            { 404, "https://static.wikia.nocookie.net/skylanders/images/c/c7/L-Bash.jpg/revision/"
                   "latest?cb=20120406091737" }, // Legendary Bash
            { 416, "https://static.wikia.nocookie.net/skylanders/images/2/2c/L-Spyro.jpg/revision/"
                   "latest?cb=20120221175411" }, // Legendary Spyro
            { 419, "https://static.wikia.nocookie.net/skylanders/images/b/b1/Legendary_Trigger_Happy.jpg/revision/"
                   "latest?cb=20120424173458" }, // Legendary Trigger Happy
            { 430, "https://static.wikia.nocookie.net/skylanders/images/c/cb/L-Chop_Chop.jpg/revision/"
                   "latest?cb=20120221175326" }, // Legendary Chop Chop
            { 450, "https://static.wikia.nocookie.net/skylanders/images/4/49/Gusto_Promo.jpg/revision/"
                   "latest?cb=20140724193737" }, // Gusto
            { 451, "https://static.wikia.nocookie.net/skylanders/images/a/a4/Thunderbolt.jpg/revision/"
                   "latest?cb=20150901011301" }, // Thunderbolt
            { 452, "https://static.wikia.nocookie.net/skylanders/images/f/f9/Fling_Kong.jpg/revision/"
                   "latest?cb=20150901011412" }, // Fling Kong
            { 453, "https://static.wikia.nocookie.net/skylanders/images/f/ff/Blades.png/revision/"
                   "latest?cb=20170304031523" }, // Blades
            { 454, "https://static.wikia.nocookie.net/skylanders/images/3/30/Wallop_Promo.jpg/revision/"
                   "latest?cb=20140424001100" }, // Wallop
            { 455, "https://static.wikia.nocookie.net/skylanders/images/7/7b/Head_Rush_Promo.jpg/revision/"
                   "latest?cb=20140813084343" }, // Head Rush
            { 456, "https://static.wikia.nocookie.net/skylanders/images/4/44/Fist_Bump_Promo.jpg/revision/"
                   "latest?cb=20140917183945" }, // Fist Bump
            { 457, "https://static.wikia.nocookie.net/skylanders/images/f/f4/Rocky_Roll_Promo.jpg/revision/"
                   "latest?cb=20150315230107" }, // Rocky Roll
            { 458, "https://static.wikia.nocookie.net/skylanders/images/b/b9/Wildfire_Promo.jpg/revision/"
                   "latest?cb=20140610214032" }, // Wildfire
            { 459, "https://static.wikia.nocookie.net/skylanders/images/6/6e/Kaboom_Promo.jpg/revision/"
                   "latest?cb=20140813084343" }, // Ka-Boom
            { 460, "https://static.wikia.nocookie.net/skylanders/images/1/11/Trail_Blazier_Promo.png/revision/"
                   "latest?cb=20150527220904" }, // Trail Blazer
            { 461, "https://static.wikia.nocookie.net/skylanders/images/1/1b/Torch_Promo.jpg/revision/"
                   "latest?cb=20140610214032" }, // Torch
            { 462, "https://static.wikia.nocookie.net/skylanders/images/2/26/Snap_Shot_Promo.jpg/revision/"
                   "latest?cb=20140424001100" }, // Snap Shot
            { 463, "https://static.wikia.nocookie.net/skylanders/images/b/b5/Lob-Star.png/revision/"
                   "latest?cb=20141013023227" }, // Lob-Star
            { 464, "https://static.wikia.nocookie.net/skylanders/images/2/2a/Flip_Wreck.jpg/revision/"
                   "latest?cb=20161214171853" }, // Flip Wreck
            { 465, "https://static.wikia.nocookie.net/skylanders/images/a/a0/Echo.jpg/revision/"
                   "latest?cb=20221225035655" }, // Echo
            { 466, "https://static.wikia.nocookie.net/skylanders/images/f/f1/Blastermind_Promo.jpg/revision/"
                   "latest?cb=20141212181923" }, // Blastermind
            { 467, "https://static.wikia.nocookie.net/skylanders/images/0/09/Enigma_Art.png/revision/"
                   "latest?cb=20171116073607" }, // Enigma
            { 468, "https://static.wikia.nocookie.net/skylanders/images/4/41/D%C3%A9j%C3%A0_Vu_Promo.jpg/revision/"
                   "latest?cb=20140917183946" }, // Deja Vu
            { 469, "https://static.wikia.nocookie.net/skylanders/images/2/2d/Cobra_Cadabra_Promo.jpg/revision/"
                   "latest?cb=20141001165633" }, // Cobra Cadabra
            { 470, "https://static.wikia.nocookie.net/skylanders/images/1/16/Jawbreaker_Promo.jpg/revision/"
                   "latest?cb=20140610214032" }, // Jawbreaker
            { 471, "https://static.wikia.nocookie.net/skylanders/images/4/42/Gearshift_Promo.jpg/revision/"
                   "latest?cb=20140610214032" }, // Gearshift
            { 472, "https://static.wikia.nocookie.net/skylanders/images/6/67/Chopper_Promo.jpg/revision/"
                   "latest?cb=20140424001102" }, // Chopper
            { 473, "https://static.wikia.nocookie.net/skylanders/images/d/d1/Tread_Head_Promo.jpg/revision/"
                   "latest?cb=20141001165633" }, // Tread Head
            { 474, "https://static.wikia.nocookie.net/skylanders/images/6/68/Bushwhack_Promo.jpg/revision/"
                   "latest?cb=20140813084342" }, // Bushwhack
            { 475, "https://static.wikia.nocookie.net/skylanders/images/5/51/Tuff_Luck_Promo.jpg/revision/"
                   "latest?cb=20240521091017" }, // Tuff Luck
            { 476, "https://static.wikia.nocookie.net/skylanders/images/c/c7/Food_Fight_Promo.jpg/revision/"
                   "latest?cb=20140424001100" }, // Food Fight
            { 477, "https://static.wikia.nocookie.net/skylanders/images/5/59/High_Five.jpg/revision/"
                   "latest?cb=20140922203655" }, // High Five
            { 478, "https://static.wikia.nocookie.net/skylanders/images/4/44/Krypt_King_Promo.jpg/revision/"
                   "latest?cb=20140619165830" }, // Krypt King
            { 479, "https://static.wikia.nocookie.net/skylanders/images/8/86/Short_Cut_Promo.jpg/revision/"
                   "latest?cb=20141001165634" }, // Short Cut
            { 480, "https://static.wikia.nocookie.net/skylanders/images/0/05/Bat_Spin_Promo2.jpg/revision/"
                   "latest?cb=20150320233553" }, // Bat Spin
            { 481, "https://static.wikia.nocookie.net/skylanders/images/2/24/Funny_Bone.png/revision/"
                   "latest?cb=20171116075227" }, // Funny Bone
            { 482, "https://static.wikia.nocookie.net/skylanders/images/4/40/Knight_Light_Promo.jpg/revision/"
                   "latest?cb=20141218040405" }, // Knight Light
            { 483, "https://static.wikia.nocookie.net/skylanders/images/8/8b/Spotlight_Promo.jpg/revision/"
                   "latest?cb=20221225040407" }, // Spotlight
            { 484, "https://static.wikia.nocookie.net/skylanders/images/7/79/Knight_Mare_Promo.jpg/revision/"
                   "latest?cb=20141218040407" }, // Knight Mare
            { 485, "https://static.wikia.nocookie.net/skylanders/images/4/42/Blackout_Promo.jpg/revision/"
                   "latest?cb=20141218040403" }, // Blackout
            { 502, "https://static.wikia.nocookie.net/skylanders/images/1/1d/Bop_Promo.jpg/revision/"
                   "latest?cb=20140813084341" }, // Bop
            { 503, "https://static.wikia.nocookie.net/skylanders/images/2/22/Spry_Promo.jpg/revision/"
                   "latest?cb=20140813084341" }, // Spry
            { 504, "https://static.wikia.nocookie.net/skylanders/images/0/0c/Hijinx_Promo.jpg/revision/"
                   "latest?cb=20140813084342" }, // Hijinx
            { 505, "https://static.wikia.nocookie.net/skylanders/images/3/37/Terrabite_promo.jpg/revision/"
                   "latest?cb=20140813155206" }, // Terrabite
            { 506, "https://static.wikia.nocookie.net/skylanders/images/0/0f/Breeze_Promo.jpg/revision/"
                   "latest?cb=20140813084340" }, // Breeze
            { 507, "https://static.wikia.nocookie.net/skylanders/images/9/9a/Weeruptor_Promo.jpg/revision/"
                   "latest?cb=20140813084344" }, // Weeruptor
            { 508, "https://static.wikia.nocookie.net/skylanders/images/4/45/PetVac_Promo.jpg/revision/"
                   "latest?cb=20140813084341" }, // Pet Vac
            { 509, "https://static.wikia.nocookie.net/skylanders/images/a/ad/Small_Fry_Promo.jpg/revision/"
                   "latest?cb=20140813084342" }, // Small Fry
            { 510, "https://static.wikia.nocookie.net/skylanders/images/e/e3/Drobit_Promo.jpg/revision/"
                   "latest?cb=20140813084344" }, // Drobit
            { 514, "https://static.wikia.nocookie.net/skylanders/images/c/c7/Gil_Runt_Promo.jpg/revision/"
                   "latest?cb=20140813155241" }, // Gill Runt
            { 519, "https://static.wikia.nocookie.net/skylanders/images/c/ce/Trigger_Snappy_Promo.jpg/revision/"
                   "latest?cb=20140813155329" }, // Trigger Snappy
            { 526, "https://static.wikia.nocookie.net/skylanders/images/0/0e/Whisper_Elf_Promo.jpg/revision/"
                   "latest?cb=20140813155119" }, // Whisper Elf
            { 540, "https://static.wikia.nocookie.net/skylanders/images/4/44/Barkley_Promo.jpg/revision/"
                   "latest?cb=20140813155839" }, // Barkley
            { 541, "https://static.wikia.nocookie.net/skylanders/images/c/ce/Thumpling.jpg/revision/"
                   "latest?cb=20140813160158" }, // Thumpling
            { 542, "https://static.wikia.nocookie.net/skylanders/images/a/a3/Mini_jini.jpg/revision/"
                   "latest?cb=20140813160310" }, // Mini-Jini
            { 543, "https://static.wikia.nocookie.net/skylanders/images/9/98/Eye_Small.jpg/revision/"
                   "latest?cb=20140813160037" }, // Eye-Small
            { 601, "https://static.wikia.nocookie.net/skylanders/images/c/cf/King_Pen_Promo.jpg/revision/"
                   "latest?cb=20160601124404" }, // King Pen
            { 602, "https://static.wikia.nocookie.net/skylanders/images/9/99/Tri_Tip_Promo.jpg/revision/"
                   "latest?cb=20160601125516" }, // Tri-Tip
            { 603, "https://static.wikia.nocookie.net/skylanders/images/6/6c/Chopscotch.png/revision/"
                   "latest?cb=20160817103507" }, // Chopscotch
            { 604, "https://static.wikia.nocookie.net/skylanders/images/e/ef/Boom_Bloom_Promo.jpg/revision/"
                   "latest?cb=20161112054948" }, // Boom Bloom
            { 605, "https://static.wikia.nocookie.net/skylanders/images/b/b0/Pit_Boss_Promo.jpg/revision/"
                   "latest?cb=20160914194009" }, // Pit Boss
            { 606, "https://static.wikia.nocookie.net/skylanders/images/6/6e/Barbella_Promo.jpg/revision/"
                   "latest?cb=20160927200305" }, // Barbella
            { 607, "https://static.wikia.nocookie.net/skylanders/images/7/75/Air_Strike_Promo.jpg/revision/"
                   "latest?cb=20161023085357" }, // Air Strike
            { 608, "https://static.wikia.nocookie.net/skylanders/images/8/87/Emberpromo.jpg/revision/"
                   "latest?cb=20160615082817" }, // Ember
            { 609, "https://static.wikia.nocookie.net/skylanders/images/8/85/Ambush_Promo.jpg/revision/"
                   "latest?cb=20160614032110" }, // Ambush
            { 610, "https://static.wikia.nocookie.net/skylanders/images/2/2d/Dr._Krankcase_Promo.jpg/revision/"
                   "latest?cb=20141001165635" }, // Dr. Krankcase
            { 611, "https://static.wikia.nocookie.net/skylanders/images/a/aa/Hood-Sickle_Promo.jpg/revision/"
                   "latest?cb=20140915173910" }, // Hood Sickle
            { 612, "https://static.wikia.nocookie.net/skylanders/images/5/5e/Tae_Kown_Crow.png/revision/"
                   "latest?cb=20150103194222" }, // Tae Kwon Crow
            { 613, "https://static.wikia.nocookie.net/skylanders/images/1/19/Golden_Queen_Promo_Art.jpeg/revision/"
                   "latest?cb=20230111120332" }, // Golden Queen
            { 614, "https://static.wikia.nocookie.net/skylanders/images/1/13/Wolfgang_Promo.jpg/revision/"
                   "latest?cb=20140610214033" }, // Wolfgang
            { 615, "https://static.wikia.nocookie.net/skylanders/images/f/f0/PAINYATTAS.png/revision/"
                   "latest?cb=20221105014144" }, // Pain-Yatta
            { 616, "https://static.wikia.nocookie.net/skylanders/images/6/6d/Mysticat_Promo.jpg/revision/"
                   "latest?cb=20160927195737" }, // Mysticat
            { 617, "https://static.wikia.nocookie.net/skylanders/images/f/f2/SI_Ilus_Starcast_FINAL.png/revision/"
                   "latest?cb=20160817073729" }, // Starcast
            { 618, "https://static.wikia.nocookie.net/skylanders/images/d/dc/Buckshot_Promo.jpg/revision/"
                   "latest?cb=20161112054954" }, // Buckshot
            { 619, "https://static.wikia.nocookie.net/skylanders/images/8/83/Aurora_Promo.jpg/revision/"
                   "latest?cb=20160927195313" }, // Aurora
            { 620, "https://static.wikia.nocookie.net/skylanders/images/b/bd/Flare_Wolf_Promo.jpg/revision/"
                   "latest?cb=20161112054950" }, // Flare Wolf
            { 621, "https://static.wikia.nocookie.net/skylanders/images/e/e4/Chompy_Mage_Promo.jpg/revision/"
                   "latest?cb=20140611103634" }, // Chompy Mage
            { 622, "https://static.wikia.nocookie.net/skylanders/images/0/01/BadJujuRoHRender.png/revision/"
                   "latest?cb=20221006032834" }, // Bad Juju
            { 623, "https://static.wikia.nocookie.net/skylanders/images/2/25/GraveClobber2.png/revision/"
                   "latest?cb=20210213115240" }, // Grave Clobber
            { 624, "https://static.wikia.nocookie.net/skylanders/images/0/0b/BlasterTron2.png/revision/"
                   "latest?cb=20210212225523" }, // Blaster-Tron
            { 625, "https://static.wikia.nocookie.net/skylanders/images/8/82/Ro-Bow_Promo.jpg/revision/"
                   "latest?cb=20161112054954" }, // Ro-Bow
            { 626, "https://static.wikia.nocookie.net/skylanders/images/8/89/Chain_Reaction_Promo.jpg/revision/"
                   "latest?cb=20161112054955" }, // Chain Reaction
            { 627, "https://static.wikia.nocookie.net/skylanders/images/9/9e/Kaos_Promo.jpg/revision/"
                   "latest?cb=20140509174634" }, // Kaos
            { 628, "https://static.wikia.nocookie.net/skylanders/images/8/88/Wild_Storm_Artwork.jpg/revision/"
                   "latest?cb=20161112115632" }, // Wild Storm
            { 629, "https://static.wikia.nocookie.net/skylanders/images/6/6e/Tidepool_Promo.jpg/revision/"
                   "latest?cb=20161112054952" }, // Tidepool
            { 630, "https://static.wikia.nocookie.net/skylanders/images/0/0b/Crash_Bandicoot_Imaginators_Art.jpg/"
                   "revision/latest?cb=20160818014103" }, // Crash Bandicoot
            { 631, "https://static.wikia.nocookie.net/skylanders/images/3/3c/SI_Ilus_DrNeoCortex_FINAL_HiRes.jpg/"
                   "revision/latest?cb=20160907090145" }, // Dr. Neo Cortex
            { 1000, "https://static.wikia.nocookie.net/skylanders/images/9/9f/Boom_Jet_Promo_Art.jpg/revision/"
                    "latest?cb=20130820183143" }, // Boom Jet (bottom)
            { 1001, "https://static.wikia.nocookie.net/skylanders/images/1/1c/Free_Ranger_Art.jpg/revision/"
                    "latest?cb=20160119001912" }, // Free Ranger (bottom)
            { 1002, "https://static.wikia.nocookie.net/skylanders/images/1/1d/Rubble_Rouser_%282%29.png/revision/"
                    "latest?cb=20140131013705" }, // Rubble Rouser (bottom)
            { 1003, "https://static.wikia.nocookie.net/skylanders/images/1/14/Doom_Stone_Promo.jpg/revision/"
                    "latest?cb=20140523034255" }, // Doom Stone (bottom)
            { 1004, "https://static.wikia.nocookie.net/skylanders/images/b/ba/Blast_Zone_Art.jpg/revision/"
                    "latest?cb=20130819183648" }, // Blast Zone (bottom)
            { 1005, "https://static.wikia.nocookie.net/skylanders/images/1/17/Firekrakenartwork.jpg/revision/"
                    "latest?cb=20131230180207" }, // Fire Kraken (bottom)
            { 1006, "https://static.wikia.nocookie.net/skylanders/images/f/ff/Stink_Bomb_Promo_Art.jpg/revision/"
                    "latest?cb=20130819172838" }, // Stink Bomb (bottom)
            { 1007, "https://static.wikia.nocookie.net/skylanders/images/c/c7/Grilla_Drilla_Promo_Art.jpg/revision/"
                    "latest?cb=20130821200813" }, // Grilla Drilla (bottom)
            { 1008, "https://static.wikia.nocookie.net/skylanders/images/7/77/HootLoop2.jpg/revision/"
                    "latest?cb=20130819002110" }, // Hoot Loop (bottom)
            { 1009, "https://static.wikia.nocookie.net/skylanders/images/a/a7/Trap_Shadow_Promo.jpg/revision/"
                    "latest?cb=20140313203454" }, // Trap Shadow (bottom)
            { 1010, "https://static.wikia.nocookie.net/skylanders/images/d/d0/Magna_Charge_Art.jpg/revision/"
                    "latest?cb=20130908180141" }, // Magna Charge (bottom)
            { 1011, "https://static.wikia.nocookie.net/skylanders/images/a/a4/Spyrise.jpg/revision/"
                    "latest?cb=20131231034730" }, // Spy Rise (bottom)
            { 1012, "https://static.wikia.nocookie.net/skylanders/images/d/d8/Night_Shift_Art.jpg/revision/"
                    "latest?cb=20130819001655" }, // Night Shift (bottom)
            { 1013, "https://static.wikia.nocookie.net/skylanders/images/3/31/RattleShake2.jpg/revision/"
                    "latest?cb=20130819000745" }, // Rattle Shake (bottom)
            { 1014, "https://static.wikia.nocookie.net/skylanders/images/a/a6/Freeze_Blade_Promo.jpg/revision/"
                    "latest?cb=20140313203456" }, // Freeze Blade (bottom)
            { 1015, "https://static.wikia.nocookie.net/skylanders/images/6/63/Wash_Buckler_Art.jpg/revision/"
                    "latest?cb=20130819015552" }, // Wash Buckler (bottom)
            { 2000, "https://static.wikia.nocookie.net/skylanders/images/9/9f/Boom_Jet_Promo_Art.jpg/revision/"
                    "latest?cb=20130820183143" }, // Boom Jet (top)
            { 2001, "https://static.wikia.nocookie.net/skylanders/images/1/1c/Free_Ranger_Art.jpg/revision/"
                    "latest?cb=20160119001912" }, // Free Ranger (top)
            { 2002, "https://static.wikia.nocookie.net/skylanders/images/1/1d/Rubble_Rouser_%282%29.png/revision/"
                    "latest?cb=20140131013705" }, // Rubble Rouser (top)
            { 2003, "https://static.wikia.nocookie.net/skylanders/images/1/14/Doom_Stone_Promo.jpg/revision/"
                    "latest?cb=20140523034255" }, // Doom Stone (top)
            { 2004, "https://static.wikia.nocookie.net/skylanders/images/b/ba/Blast_Zone_Art.jpg/revision/"
                    "latest?cb=20130819183648" }, // Blast Zone (top)
            { 2005, "https://static.wikia.nocookie.net/skylanders/images/1/17/Firekrakenartwork.jpg/revision/"
                    "latest?cb=20131230180207" }, // Fire Kraken (top)
            { 2006, "https://static.wikia.nocookie.net/skylanders/images/f/ff/Stink_Bomb_Promo_Art.jpg/revision/"
                    "latest?cb=20130819172838" }, // Stink Bomb (top)
            { 2007, "https://static.wikia.nocookie.net/skylanders/images/c/c7/Grilla_Drilla_Promo_Art.jpg/revision/"
                    "latest?cb=20130821200813" }, // Grilla Drilla (top)
            { 2008, "https://static.wikia.nocookie.net/skylanders/images/7/77/HootLoop2.jpg/revision/"
                    "latest?cb=20130819002110" }, // Hoot Loop (top)
            { 2009, "https://static.wikia.nocookie.net/skylanders/images/a/a7/Trap_Shadow_Promo.jpg/revision/"
                    "latest?cb=20140313203454" }, // Trap Shadow (top)
            { 2010, "https://static.wikia.nocookie.net/skylanders/images/d/d0/Magna_Charge_Art.jpg/revision/"
                    "latest?cb=20130908180141" }, // Magna Charge (top)
            { 2011, "https://static.wikia.nocookie.net/skylanders/images/a/a4/Spyrise.jpg/revision/"
                    "latest?cb=20131231034730" }, // Spy Rise (top)
            { 2012, "https://static.wikia.nocookie.net/skylanders/images/d/d8/Night_Shift_Art.jpg/revision/"
                    "latest?cb=20130819001655" }, // Night Shift (top)
            { 2013, "https://static.wikia.nocookie.net/skylanders/images/3/31/RattleShake2.jpg/revision/"
                    "latest?cb=20130819000745" }, // Rattle Shake (top)
            { 2014, "https://static.wikia.nocookie.net/skylanders/images/a/a6/Freeze_Blade_Promo.jpg/revision/"
                    "latest?cb=20140313203456" }, // Freeze Blade (top)
            { 2015, "https://static.wikia.nocookie.net/skylanders/images/6/63/Wash_Buckler_Art.jpg/revision/"
                    "latest?cb=20130819015552" }, // Wash Buckler (top)
            { 3000, "https://static.wikia.nocookie.net/skylanders/images/8/8f/Scratch_Promo.jpg/revision/"
                    "latest?cb=20140313203454" }, // Scratch
            { 3001, "https://static.wikia.nocookie.net/skylanders/images/6/6b/Pop_Thorn.jpg/revision/"
                    "latest?cb=20131019000653" }, // Pop Thorn
            { 3002, "https://static.wikia.nocookie.net/skylanders/images/3/3c/Slobber_Tooth_Promo_Art.jpg/revision/"
                    "latest?cb=20130821081127" }, // Slobber Tooth
            { 3003, "https://static.wikia.nocookie.net/skylanders/images/1/1a/Scorp-promo.jpg/revision/"
                    "latest?cb=20140617030938" }, // Scorp
            { 3004, "https://static.wikia.nocookie.net/skylanders/images/c/c9/Fryno_Promo.jpg/revision/"
                    "latest?cb=20140307232805" }, // Fryno
            { 3005, "https://static.wikia.nocookie.net/skylanders/images/2/2c/Smolderdash.png/revision/"
                    "latest?cb=20131231034325" }, // Smolderdash
            { 3006, "https://static.wikia.nocookie.net/skylanders/images/c/cf/Bumble_blast.jpg/revision/"
                    "latest?cb=20131031002520" }, // Bumble Blast
            { 3007, "https://static.wikia.nocookie.net/skylanders/images/f/f7/Zoo_Lou_Promo_Art.jpg/revision/"
                    "latest?cb=20130821200845" }, // Zoo Lou
            { 3008, "https://static.wikia.nocookie.net/skylanders/images/c/ce/Dune_Bug_Promo.jpg/revision/"
                    "latest?cb=20141208065814" }, // Dune Bug
            { 3009, "https://static.wikia.nocookie.net/skylanders/images/0/03/Star_Strike.jpg/revision/"
                    "latest?cb=20140321200849" }, // Star Strike
            { 3010, "https://static.wikia.nocookie.net/skylanders/images/6/6b/Countdown_Art.jpg/revision/"
                    "latest?cb=20130819001743" }, // Countdown
            { 3011, "https://static.wikia.nocookie.net/skylanders/images/8/86/Windup.jpg/revision/"
                    "latest?cb=20131231034228" }, // Wind-Up
            { 3012, "https://static.wikia.nocookie.net/skylanders/images/8/8c/Roller_Brawl2.jpg/revision/"
                    "latest?cb=20130821200659" }, // Roller Brawl
            { 3013, "https://static.wikia.nocookie.net/skylanders/images/4/4b/Grim_Creeper_Promo_Art.jpg/revision/"
                    "latest?cb=20131019000545" }, // Grim Creeper
            { 3014, "https://static.wikia.nocookie.net/skylanders/images/3/3a/Rip_Tide_cpromo.jpg/revision/"
                    "latest?cb=20140617031054" }, // Rip Tide
            { 3015, "https://static.wikia.nocookie.net/skylanders/images/f/f0/Punkshock.jpg/revision/"
                    "latest?cb=20131231034556" }, // Punk Shock
            { 3400, "https://static.wikia.nocookie.net/skylanders/images/8/8e/Fiesta_Art.jpg/revision/"
                    "latest?cb=20150620202422" }, // Fiesta
            { 3401, "https://static.wikia.nocookie.net/skylanders/images/0/01/High_Volt_Art.jpg/revision/"
                    "latest?cb=20160801161736" }, // High Volt
            { 3402, "https://static.wikia.nocookie.net/skylanders/images/4/44/Splat_Promo.jpg/revision/"
                    "latest?cb=20150907034326" }, // Splat
            { 3406, "https://static.wikia.nocookie.net/skylanders/images/3/3e/Stormblade.jpg/revision/"
                    "latest?cb=20150603210039" }, // Stormblade
            { 3411, "https://static.wikia.nocookie.net/skylanders/images/e/e7/Smash_Hit_Promo.jpg/revision/"
                    "latest?cb=20151006092205" }, // Smash Hit
            { 3412, "https://static.wikia.nocookie.net/skylanders/images/f/f4/Spitfire_Promo.jpg/revision/"
                    "latest?cb=20150603211841" }, // Spitfire
            { 3413, "https://static.wikia.nocookie.net/skylanders/images/d/d1/Hurricane_Jet-Vac_Art.jpg/revision/"
                    "latest?cb=20150831080907" }, // Hurricane Jet-Vac
            { 3414, "https://static.wikia.nocookie.net/skylanders/images/7/73/Double_Dare_Trigger_Happy_Art.jpg/"
                    "revision/latest?cb=20150831080427" }, // Double Dare Trigger Happy
            { 3415, "https://static.wikia.nocookie.net/skylanders/images/b/b5/SuperShot_Stealth_Elf_Promo.jpg/revision/"
                    "latest?cb=20150603223605" }, // Super Shot Stealth Elf
            { 3416, "https://static.wikia.nocookie.net/skylanders/images/4/45/Shark_Shooter_Terrafin.jpg/revision/"
                    "latest?cb=20150619191845" }, // Shark Shooter Terrafin
            { 3417, "https://static.wikia.nocookie.net/skylanders/images/3/39/Bone_Bash_Roller_Brawl_Art.jpg/revision/"
                    "latest?cb=20150831080428" }, // Bone Bash Roller Brawl
            { 3420, "https://static.wikia.nocookie.net/skylanders/images/0/01/Big_Bubble_Pop_Fizz_Promo.jpg/revision/"
                    "latest?cb=20150907034533" }, // Big Bubble Pop Fizz
            { 3421, "https://static.wikia.nocookie.net/skylanders/images/0/01/Lava_Lance_Eruptor_Artwork.jpeg/revision/"
                    "latest?cb=20151110231734" }, // Lava Lance Eruptor
            { 3422, "https://static.wikia.nocookie.net/skylanders/images/5/59/Deep_Dive_Gill_Grunt.jpg/revision/"
                    "latest?cb=20150619185921" }, // Deep Dive Gill Grunt
            { 3423, "https://static.wikia.nocookie.net/skylanders/images/9/92/DonkeyKong_Skylanders.jpg/revision/"
                    "latest?cb=20150909223215" }, // Turbo Charge Donkey Kong
            { 3424, "https://static.wikia.nocookie.net/skylanders/images/1/17/Bowser_Art.jpg/revision/"
                    "latest?cb=20151207114002" }, // Hammer Slam Bowser
            { 3425, "https://static.wikia.nocookie.net/skylanders/images/7/72/Dive-Clops_Promo.jpg/revision/"
                    "latest?cb=20150621194954" }, // Dive-Clops
            { 3426, "https://static.wikia.nocookie.net/skylanders/images/8/89/Astroblast_Promo_Art.png/revision/"
                    "latest?cb=20260903213913" }, // Astroblast
            { 3427, "https://static.wikia.nocookie.net/skylanders/images/6/6a/Nightfall_Art.jpg/revision/"
                    "latest?cb=20150831080908" }, // Nightfall
            { 3428, "https://static.wikia.nocookie.net/skylanders/images/8/88/Thrillipede_Promo.jpg/revision/"
                    "latest?cb=20150915190830" }, // Thrillipede
        } };
    }

    std::string FigureImageUrl(uint16_t toyType)
    {
        auto it = std::lower_bound(FigureImages.begin(), FigureImages.end(), toyType,
            [](const auto& entry, uint16_t type) { return entry.first < type; });
        return it != FigureImages.end() && it->first == toyType ? std::string(it->second) : std::string();
    }
}
