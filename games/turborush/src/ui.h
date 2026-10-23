/* Native 320 x 240 menus. Every screen has a single clear primary action. */
static void footer(const char*s){gfx_fill(0,223,320,17,INK);gfx_text_center(&np_font_small,160,235,s,MUTED,256);}
static void page(const char*s){gfx_vgrad(0,0,320,240,0x0b1926,0x203b4b);gfx_text(&np_font_title,16,29,s,WHITE,256);money();}
static void menu(void){
 gfx_fill_alpha(0,0,320,240,INK,140);logo(2,HEX(0x85392e));logo(0,GOLD);
 gfx_text_center(&np_font_small,160,64,"ARCADE RACING",WHITE,256);
 static const char*const names[]={"Play","Garage","Records","How to play"};
 for(int i=0;i<4;i++){
   int y=82+i*32;gfx_rrect(14,y,151,28,5,i==selected?GOLD:INK,250);
   gfx_text(&np_font_body,26,y+20,names[i],i==selected?INK:WHITE,256);
   gfx_text_right(&np_font_body,153,y+20,">",i==selected?INK:MUTED,256);
 }
 gfx_rrect(177,82,130,124,5,INK,245);
 gfx_text_center(&np_font_small,242,100,"YOUR NEXT DRIVE",GOLD,256);
 gfx_text_center(&np_font_body,242,121,MODES[profile.mode],WHITE,256);
 tr_car(242,179,96,tr_paints[profile.paint],0,false,false);
 gfx_text_center(&np_font_small,242,190,profile.mode?tr_tracks[profile.track].name:"6 circuits / 3 laps",MUTED,256);
 char b[28];txt(num(b,profile.money)," credits");small(18,218,"By Bobydeluxe",MUTED);gfx_text_right(&np_font_small,302,218,b,GOLD,256);
 footer("Up / Down: select    OK: open    Back: exit");
}
static void setup(void){
 page("RACE SETUP");
 static const char*const names[]={"Race mode","Circuit","Time of day","Gearbox","Rivals","Weather"};
 static const char*const hints[]={"Grand Prix: six races. Single race: one circuit.","Choose where to race.","All circuits support each time of day.","Manual: shift up with 7, down with 4.","Easy is recommended for your first race.","Rain changes the atmosphere, not tire grip.","Up: throttle   Arrows: steer   OK: nitro"};
 for(int i=0;i<6;i++){
   bool disabled=i==1&&!profile.mode,focus=selected==i;int y=43+i*23;
   gfx_rrect(14,y,292,21,3,focus?GOLD:INK,256);color_t c=focus?INK:disabled?HEX(0x728793):WHITE;
   gfx_text(&np_font_small,24,y+15,i==4&&profile.mode==TR_FREE_RIDE?"Traffic":names[i],c,256);
   const char*v=i==0?MODES[profile.mode]:i==1?(profile.mode?tr_tracks[profile.track].name:"All six circuits"):i==2?MOODS[profile.mood]:i==3?(profile.manual?"Manual":"Automatic"):i==4?(profile.mode==TR_FREE_RIDE?(const char*const[]){"Light","Normal","Busy"}[profile.difficulty]:DIFF[profile.difficulty]):profile.effects==2?"Rain":"Clear";
   gfx_text_right(&np_font_small,283,y+15,v,c,256);if(!disabled)small(292,y+15,">",c);
 }
 const char*hint=hints[selected];if(profile.mode==TR_FREE_RIDE&&selected==0)hint="Endless driving. Pass traffic at your own pace.";if(profile.mode==TR_FREE_RIDE&&selected==4)hint="Choose how much traffic is on the road.";
 gfx_text_center(&np_font_small,160,191,hint,MUTED,256);
 button(197,profile.mode==TR_FREE_RIDE?"Start Free Ride":profile.mode?"Start race":"Start Grand Prix",selected==6);
 footer("Left / Right: change    OK: confirm    Back: menu");
}
static void garage(void){
 page("GARAGE");gfx_fill(28,91,264,1,HEX(0x5e7f88));tr_car(160,90,112,tr_paints[profile.paint],0,false,false);
 for(int i=0;i<4;i++){
   bool focus=selected==i;int y=99+i*24;gfx_rrect(14,y,292,22,4,focus?GOLD:INK,256);color_t c=focus?INK:WHITE;
   small(24,y+15,UPGRADES[i],c);
   for(int k=0;k<4;k++)gfx_fill(100+k*14,y+7,10,7,k<profile.upgrades[i]?focus?INK:GOLD:HEX(0x5c7180));
   char s[26];unsigned cost=tr_price(&profile,i);if(cost)txt(num(s,cost)," credits");else txt(s,"MAX");
   gfx_text_right(&np_font_small,294,y+15,s,c,256);
 }
 gfx_rrect(14,195,292,23,4,selected==4?GOLD:INK,256);small(24,211,"Paint",selected==4?INK:WHITE);
 for(int i=0;i<6;i++){gfx_fill(110+i*30,201,22,11,tr_paints[i]);if(i==profile.paint)gfx_rrect_outline(108+i*30,199,26,15,2,1,WHITE,256);}
 footer(garage_notice[0]?garage_notice:selected==4?"Left / Right: paint    Back: return":"OK: buy upgrade    Up / Down: select    Back: return");
}
static void records(void){
 page("CIRCUIT RECORDS");
 for(int i=0;i<6;i++){
   int y=45+i*24;gfx_rrect(14,y,292,22,3,INK,256);small(24,y+15,tr_tracks[i].name,WHITE);
   char s[26];if(profile.best[i])time_text(s,profile.best[i]);else txt(s,"No finish yet");gfx_text_right(&np_font_small,294,y+15,s,GOLD,256);
 }
 char s[64],*p=num(s,profile.races);p=txt(p," races finished    ");p=num(p,profile.wins);txt(p," wins");gfx_text_center(&np_font_small,160,210,s,MUTED,256);
 footer("OK / Back: menu");
}
static void panel(const char*s){gfx_fill_alpha(0,0,320,240,INK,180);gfx_rrect(16,18,288,204,7,INK,256);heading(47,s);}
static void results(void){
 panel(race.phase==TR_WRECKED?"RACE ENDED":race.rank==1?"VICTORY!":"RACE COMPLETE");char s[64],*p;
 small(28,67,tr_tracks[race.track].name,MUTED);
 if(race.phase==TR_WRECKED)txt(s,"DNF");else txt(num(txt(s,"P"),race.rank)," / 8");gfx_text_right(&np_font_body,291,68,s,GOLD,256);
 p=txt(s,"Earned ");p=num(p,prize);txt(p," credits");mid(160,91,s,WHITE);
 if(championship){p=txt(s,"Grand Prix ");p=num(p,champ_round+1);p=txt(p,"/6    Standing ");p=num(p,champ_rank());txt(p,"/8");}
 else {p=txt(s,"Race time ");time_text(p,race.time);}gfx_text_center(&np_font_small,160,112,s,MUTED,256);
 if(championship){p=num(s,points[0]);txt(p," championship points");}
 else{p=txt(s,"Best ");if(profile.best[race.track])time_text(p,profile.best[race.track]);else txt(p,"--:--.-");}gfx_text_center(&np_font_small,160,128,s,GOLD,256);
 button(139,championship&&champ_round<5?"Next circuit":"Race again",selected==0);
 button(166,"Garage",selected==1);button(193,"Back to menu",selected==2);
}
static void help(void){
 page("HOW TO PLAY");
 const char*const titles[]={"Drive and boost","Racing and collisions","Garage and Grand Prix","Free Ride"};mid(160,55,titles[help_page],GOLD);
 const char*const lines[4][7]={
  {"Up: accelerate. Down: brake.","Left / Right: steer smoothly.","Hold Up + OK or EXE: nitro.","Manual: 7 shifts up, 4 shifts down.","Back pauses; the race timer stops.","Home / Power: save profile and exit.","Use Easy rivals for your first race."},
  {"Drive 3 laps against seven opponents.","Brake before bends, accelerate out.","Grass slows you; barriers cause damage.","Car contact reduces speed and armor.","Nitro recharges after use.","Empty nitro must recharge before use.","At 100% damage the race ends."},
  {"Grand Prix runs all six circuits.","Finish races to earn credits.","Position points: 15/12/10/8/6/4/2/1.","A wreck scores no championship points.","Upgrade your car between races.","Paint colors are free.","Leaving discards the current race."},
  {"Play > Race mode > Free Ride.","Choose any circuit and traffic level.","Drive freely, with no lap limit.","New traffic arrives as you drive.","Distance and overtakes are displayed.","Contact slows you; there is no damage.","No race prizes or records. Back: pause."}};
 for(int i=0;i<7;i++)gfx_text_center(&np_font_small,160,80+i*19,lines[help_page][i],WHITE,256);
 footer("Left / Right: page    OK / Back: return");
}
static void scene(void*unused){
 (void)unused;
 if(screen==GARAGE){garage();return;}if(screen==HELP){help();return;}if(screen==SETUP){setup();return;}if(screen==RECORDS){records();return;}
 tr_world(&view);
 if(screen==MENU||screen==LEAVE){menu();if(screen==LEAVE){panel("QUIT TURBO RUSH?");button(97,"Keep playing",selected==0);button(135,"Save and quit",selected==1);}return;}
 tr_hud(&view);
 if(screen==PAUSE){panel("PAUSED");button(69,"Resume",selected==0);button(105,"How to play",selected==1);button(141,race.mode==TR_FREE_RIDE?"Restart ride":"Restart race",selected==2);button(177,"Back to menu",selected==3);}
 else if(screen==CONFIRM){panel(race.mode==TR_FREE_RIDE?(confirm_action?"LEAVE THIS RIDE?":"RESTART RIDE?"):confirm_action?"LEAVE THIS RACE?":"RESTART RACE?");mid(160,81,race.mode==TR_FREE_RIDE?"Ride distance will be reset.":"This race will be discarded.",MUTED);button(107,"Keep driving",selected==0);button(146,confirm_action?"Return to menu":"Restart",selected==1);}
 else if(screen==RESULT)results();
 else if(screen==SAVE_ERROR){panel("SAVE FAILED");mid(160,77,"Storage may be full.",WHITE);button(93,"Retry saving",selected==0);button(128,"Keep playing",selected==1);button(163,"Exit without saving",selected==2);}
}
