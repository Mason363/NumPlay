/* The categories and their words, each word once in all of them, at most 64 a category (a category's used words
 * are 64 bits of the save). New words go at the end of their category, so the save's bits keep their words. */
#include "words.h"

static const char FOOD[] =
  "Pizza|Burger|Hot dog|Spaghetti|Sushi|Taco|Burrito|Sandwich|Pancakes|Waffles|French fries|Fried chicken|"
  "Omelette|Soup|Salad|Rice|Noodles|Dumplings|Steak|Meatballs|Lasagna|Toast|Cereal|Porridge|Bacon|Sausage|"
  "Popcorn|Nachos|Cheese|Bread|Bagel|Croissant|Pretzel|Curry|Kebab|Fish and chips|Mashed potatoes|"
  "Grilled cheese|Chicken nuggets|Ramen|Hummus|Falafel|Ravioli|Risotto|Stew|Chili con carne|Peanut butter|Jam|"
  "Honey|Yogurt|Scrambled eggs|Fried rice|Spring rolls|Mac and cheese|Corn dog|Paella|Quesadilla|Garlic bread|"
  "Onion rings|Fish sticks";

static const char FRUITS[] =
  "Apple|Banana|Orange|Strawberry|Grape|Watermelon|Pineapple|Mango|Peach|Pear|Cherry|Lemon|Lime|Kiwi|Coconut|"
  "Blueberry|Raspberry|Blackberry|Plum|Apricot|Papaya|Pomegranate|Fig|Melon|Grapefruit|Avocado|Tangerine|"
  "Passion fruit|Dragon fruit|Lychee|Guava|Cranberry|Nectarine|Date|Persimmon|Starfruit|Gooseberry|Clementine|"
  "Cantaloupe|Jackfruit|Durian|Mandarin|Quince|Raisin|Prune|Honeydew|Kumquat|Mulberry|Plantain|Blood orange";

static const char VEGETABLES[] =
  "Carrot|Potato|Tomato|Onion|Garlic|Broccoli|Cauliflower|Cabbage|Lettuce|Spinach|Cucumber|Bell pepper|Corn|"
  "Peas|Green beans|Pumpkin|Zucchini|Eggplant|Mushroom|Celery|Asparagus|Beetroot|Radish|Leek|Sweet potato|"
  "Brussels sprouts|Kale|Artichoke|Turnip|Chili pepper|Olive|Squash|Ginger|Parsnip|Bean sprouts|Chickpeas|"
  "Lentils|Kidney beans|Okra|Shallot|Watercress|Rhubarb|Fennel|Yam|Bok choy|Pickle|Parsley|Basil|Arugula|"
  "Spring onion|Jalapeno";

static const char DESSERTS[] =
  "Ice cream|Chocolate cake|Cupcake|Cookie|Brownie|Donut|Cheesecake|Apple pie|Muffin|Pudding|Jelly|Tiramisu|"
  "Macaron|Crepe|Churros|Cotton candy|Lollipop|Candy cane|Marshmallow|Fudge|Toffee|Caramel|Meringue|Eclair|"
  "Cinnamon roll|Banana split|Sundae|Ice pop|Sorbet|Frozen yogurt|Custard|Gingerbread man|Fruit salad|"
  "Carrot cake|Birthday cake|Wedding cake|Gummy bears|Chocolate bar|Rice pudding|Baklava|Mochi|Shortbread|"
  "Lemon tart|Chocolate mousse|Panna cotta|Apple crumble|Jelly beans|Whipped cream|Cannoli|Waffle cone|"
  "Cake pop|Profiteroles|Key lime pie|Pecan pie";

static const char DRINKS[] =
  "Water|Milk|Orange juice|Apple juice|Lemonade|Hot chocolate|Tea|Coffee|Smoothie|Milkshake|Soda|Iced tea|"
  "Sparkling water|Coconut water|Chocolate milk|Fruit punch|Ginger ale|Cola|Bubble tea|Green tea|Herbal tea|"
  "Espresso|Cappuccino|Latte|Grape juice|Tomato juice|Cranberry juice|Pineapple juice|Mango lassi|Horchata|"
  "Sports drink|Slushie|Frappe|Mocha|Chai|Soy milk|Almond milk|Oat milk|Strawberry milk|Mint tea|Iced coffee|"
  "Carrot juice|Watermelon juice|Peach tea|Tonic water|Matcha|Buttermilk|Hot milk|Pink lemonade";

static const char KITCHEN[] =
  "Fridge|Oven|Microwave|Toaster|Kettle|Blender|Frying pan|Saucepan|Spatula|Whisk|Ladle|Rolling pin|"
  "Cutting board|Knife|Fork|Spoon|Plate|Bowl|Mug|Glass|Teapot|Dishwasher|Sink|Sponge|Apron|Oven mitt|Grater|"
  "Peeler|Colander|Can opener|Tongs|Measuring cup|Kitchen scale|Egg timer|Freezer|Stove|Lunch box|Napkin|"
  "Tablecloth|Chopsticks|Straw|Coffee machine|Mixer|Baking tray|Salt shaker|Pepper mill|Ice cube tray|"
  "Trash can|Dish soap|Tea towel|Jar|Wok|Rice cooker|Food processor|Cookbook|Placemat|Toothpick|Aluminum foil";

static const char ANIMALS[] =
  "Lion|Tiger|Elephant|Giraffe|Zebra|Monkey|Gorilla|Kangaroo|Koala|Panda|Bear|Polar bear|Wolf|Fox|Rabbit|"
  "Squirrel|Deer|Moose|Horse|Cow|Pig|Sheep|Goat|Chicken|Duck|Dog|Cat|Hamster|Mouse|Owl|Eagle|Parrot|Penguin|"
  "Flamingo|Peacock|Crocodile|Snake|Turtle|Frog|Camel|Hippo|Rhino|Cheetah|Leopard|Sloth|Hedgehog|Bat|Raccoon|"
  "Skunk|Beaver|Llama|Ostrich|Butterfly|Bee|Spider|Ant|Snail|Lizard|Chameleon|Donkey|Swan|Hyena|Ladybug|"
  "Guinea pig";

static const char SEA[] =
  "Shark|Whale|Dolphin|Octopus|Jellyfish|Starfish|Seahorse|Crab|Lobster|Shrimp|Squid|Clam|Oyster|Seal|"
  "Sea lion|Walrus|Sea turtle|Coral reef|Stingray|Swordfish|Clownfish|Pufferfish|Eel|Seaweed|Anchor|Shipwreck|"
  "Lighthouse|Wave|Tide|Island|Beach|Sand|Seashell|Pearl|Treasure chest|Pirate ship|Surfboard|Scuba diver|"
  "Snorkel|Fishing net|Buoy|Pier|Harbor|Iceberg|Seagull|Pelican|Orca|Narwhal|Sea urchin|Plankton|Sandcastle|"
  "Message in a bottle|Tuna|Salmon";

static const char COUNTRIES[] =
  "France|Spain|Italy|Germany|United Kingdom|Ireland|Portugal|Greece|Netherlands|Belgium|Switzerland|Sweden|"
  "Norway|Finland|Denmark|Iceland|Poland|Russia|Ukraine|Turkey|Egypt|Morocco|Nigeria|Kenya|South Africa|"
  "Ethiopia|Ghana|Madagascar|United States|Canada|Mexico|Brazil|Argentina|Chile|Peru|Colombia|Cuba|Jamaica|"
  "China|Japan|South Korea|India|Thailand|Vietnam|Indonesia|Philippines|Australia|New Zealand|Saudi Arabia|"
  "Austria|Hungary|Croatia|Pakistan|Nepal|Mongolia|Singapore|Malaysia|Venezuela|Senegal|Tunisia";

static const char CITIES[] =
  "Paris|London|New York|Tokyo|Rome|Madrid|Barcelona|Berlin|Amsterdam|Venice|Moscow|Beijing|Shanghai|"
  "Hong Kong|Seoul|Bangkok|Dubai|Cairo|Istanbul|Athens|Lisbon|Dublin|Vienna|Prague|Sydney|Melbourne|Toronto|"
  "Montreal|Vancouver|Los Angeles|San Francisco|Chicago|Las Vegas|Miami|Washington|Boston|Rio de Janeiro|"
  "Buenos Aires|Mexico City|Mumbai|Delhi|Cape Town|Nairobi|Marrakech|Stockholm|Copenhagen|Oslo|Brussels|"
  "Geneva|Florence|Milan|Naples|Edinburgh|Kyoto|Osaka|Havana|Lima|Honolulu|New Orleans|Seattle|Sao Paulo|"
  "Manila";

static const char PLACES[] =
  "Eiffel Tower|Great Wall of China|Pyramids of Giza|Statue of Liberty|Big Ben|Colosseum|Taj Mahal|"
  "Mount Everest|Niagara Falls|Grand Canyon|Stonehenge|Machu Picchu|Sydney Opera House|Golden Gate Bridge|"
  "Leaning Tower of Pisa|Mount Rushmore|Christ the Redeemer|Times Square|Amazon rainforest|Sahara Desert|"
  "North Pole|Buckingham Palace|White House|Acropolis|Petra|Angkor Wat|Louvre|Sagrada Familia|Notre Dame|"
  "Arc de Triomphe|Great Barrier Reef|Mount Fuji|Kilimanjaro|Victoria Falls|Dead Sea|Galapagos Islands|"
  "Yellowstone|Easter Island|Chichen Itza|Forbidden City|Tower Bridge|Trafalgar Square|Red Square|Central Park|"
  "Alcatraz|Hoover Dam|Panama Canal|Tower of London|Bermuda Triangle|Loch Ness|Mont Saint-Michel|Versailles|"
  "Uluru|Death Valley|South Pole|Brooklyn Bridge|Pompeii";

static const char JOBS[] =
  "Doctor|Nurse|Teacher|Firefighter|Police officer|Chef|Pilot|Farmer|Dentist|Vet|Lawyer|Judge|Mechanic|"
  "Plumber|Electrician|Carpenter|Builder|Architect|Engineer|Scientist|Artist|Musician|Singer|Actor|Dancer|"
  "Photographer|Journalist|Writer|Baker|Butcher|Cashier|Waiter|Hairdresser|Barber|Mail carrier|Taxi driver|"
  "Bus driver|Truck driver|Lifeguard|Soldier|Sailor|Fisherman|Gardener|Librarian|Cleaner|Programmer|Designer|"
  "Pharmacist|Surgeon|Paramedic|Zookeeper|Tailor|Florist|Banker|Accountant|Receptionist|Security guard|Coach|"
  "Referee|Magician|Clown|Detective|Principal|Babysitter";

static const char SPORTS[] =
  "Soccer|Basketball|Tennis|Baseball|Volleyball|Golf|Rugby|Ice hockey|Swimming|Running|Cycling|Boxing|Karate|"
  "Judo|Wrestling|Fencing|Archery|Skiing|Snowboarding|Ice skating|Surfing|Skateboarding|Gymnastics|"
  "Table tennis|Badminton|Bowling|Cricket|Handball|Diving|Rowing|Sailing|Rock climbing|Horse riding|Marathon|"
  "High jump|Long jump|Pole vault|Javelin|Triathlon|Water polo|Curling|Bobsled|Kayaking|Darts|Billiards|"
  "Weightlifting|Yoga|Parkour|Dodgeball|Lacrosse|Softball|Figure skating|Car racing|American football|"
  "Field hockey|Sumo|Taekwondo|Shot put|Hurdles";

static const char SCHOOL[] =
  "Classroom|Desk|Blackboard|Whiteboard|Chalk|Pencil|Pen|Eraser|Ruler|Backpack|Notebook|Textbook|Homework|"
  "Exam|Report card|Gym class|Cafeteria|Recess|School bus|Locker|Calculator|Scissors|Glue stick|Stapler|"
  "Highlighter|Pencil case|Pencil sharpener|Globe|Map|Math|History|Geography|Art class|Music class|Field trip|"
  "Graduation|Diploma|School bell|Hall pass|Detention|Spelling test|Science fair|School uniform|Timetable|"
  "Dictionary|Crayons|Marker|Protractor|Summer break|First day of school|Yearbook|Class photo|Quiz|Grades|"
  "Paper clip|Hole punch";

static const char HOME[] =
  "Sofa|Bed|Pillow|Blanket|Lamp|Television|Remote control|Armchair|Table|Chair|Bookshelf|Wardrobe|Mirror|"
  "Clock|Carpet|Curtains|Window|Door|Stairs|Bathtub|Shower|Toilet|Toothbrush|Towel|Soap|Shampoo|"
  "Washing machine|Vacuum cleaner|Iron|Ironing board|Broom|Mop|Bucket|Doorbell|Mailbox|Garage|Attic|Basement|"
  "Fireplace|Radiator|Ceiling fan|Light switch|Houseplant|Vase|Picture frame|Alarm clock|Laundry basket|"
  "Hair dryer|Doormat|House key|Garden|Balcony|Chimney|Bunk bed|Coat hanger|Smoke alarm|Toothpaste";

static const char CLOTHES[] =
  "T-shirt|Jeans|Dress|Skirt|Shorts|Sweater|Hoodie|Jacket|Coat|Raincoat|Scarf|Gloves|Mittens|Hat|Cap|Beanie|"
  "Socks|Shoes|Sneakers|Boots|Sandals|Flip-flops|Slippers|Pajamas|Swimsuit|Tie|Bow tie|Belt|Suit|Leggings|"
  "Overalls|Vest|Blouse|Shirt|Tracksuit|Cardigan|Bathrobe|Sunglasses|Earrings|Necklace|Bracelet|Ring|Watch|"
  "Handbag|Wallet|Umbrella|Tuxedo|Kimono|Poncho|High heels|Wedding dress|Costume|Headband|Earmuffs|"
  "Rain boots|Bandana|Turtleneck|Polo shirt";

static const char TRANSPORT[] =
  "Car|Bus|Train|Airplane|Bicycle|Motorcycle|Scooter|Skateboard|Boat|Cruise ship|Ferry|Submarine|Helicopter|"
  "Hot air balloon|Taxi|Truck|Tractor|Ambulance|Fire truck|Police car|Tram|Subway|Cable car|Canoe|Sailboat|"
  "Yacht|Jet|Glider|Parachute|Horse and carriage|Sled|Snowmobile|Limousine|Van|Camper van|Tow truck|"
  "Garbage truck|Bulldozer|Monorail|Rickshaw|Gondola|Unicycle|Tandem bike|Roller skates|Airship|"
  "Electric scooter|Go-kart|Steam train|Double-decker bus|Moped|Pickup truck|Raft|Speedboat|Rowboat|"
  "Cargo ship|Seaplane|Bullet train";

static const char INSTRUMENTS[] =
  "Piano|Guitar|Violin|Drums|Flute|Trumpet|Saxophone|Cello|Harp|Clarinet|Trombone|Tuba|Accordion|Harmonica|"
  "Ukulele|Banjo|Xylophone|Tambourine|Triangle|Bagpipes|Keyboard|Electric guitar|Bass guitar|Double bass|"
  "Viola|Oboe|Bassoon|French horn|Recorder|Maracas|Bongos|Cymbals|Organ|Synthesizer|Kazoo|Castanets|Gong|"
  "Sitar|Didgeridoo|Steel drum|Mandolin|Harpsichord|Bugle|Cowbell|Djembe|Pan flute|Glockenspiel|Snare drum|"
  "Lute|Marimba|Piccolo";

static const char NATURE[] =
  "Rain|Snow|Wind|Storm|Thunder|Lightning|Rainbow|Cloud|Fog|Hail|Tornado|Hurricane|Flood|Heatwave|Frost|"
  "Sunset|Sunrise|Mountain|Volcano|River|Lake|Waterfall|Forest|Jungle|Desert|Cave|Canyon|Valley|Hill|Meadow|"
  "Glacier|Swamp|Tree|Flower|Grass|Leaf|Rock|Mud|Puddle|Snowflake|Avalanche|Earthquake|Breeze|Sky|Pond|Cliff|"
  "Field|Icicle|Sunshine|Drizzle|Blizzard|Dew|Sandstorm|Hot spring|Geyser|Acorn|Pine cone|Moss|"
  "Northern lights";

static const char BODY[] =
  "Head|Hair|Eye|Ear|Nose|Mouth|Tooth|Tongue|Lips|Chin|Cheek|Forehead|Eyebrow|Eyelash|Neck|Shoulder|Arm|Elbow|"
  "Wrist|Hand|Finger|Thumb|Fingernail|Chest|Belly button|Stomach|Back|Knee|Leg|Ankle|Foot|Toe|Heel|Heart|"
  "Brain|Lungs|Bones|Skeleton|Skull|Muscle|Skin|Blood|Kidney|Hip|Spine|Ribs|Freckles|Dimple|Beard|Mustache|"
  "Throat|Palm|Jaw|Knuckle|Eyelid|Liver|Goosebumps|Hiccups";

static const char HOBBIES[] =
  "Reading|Painting|Drawing|Cooking|Baking|Gardening|Fishing|Hiking|Photography|Knitting|Sewing|Dancing|"
  "Singing|Video games|Jigsaw puzzles|Stamp collecting|Bird watching|Origami|Pottery|Magic tricks|Juggling|"
  "Writing stories|Karaoke|Woodworking|Scrapbooking|Calligraphy|Model trains|Coding|Blogging|Beekeeping|"
  "Kite flying|Stargazing|Coin collecting|Crochet|Embroidery|Film making|Comic books|Doodling|Building models|"
  "Learning languages|Cosplay|Volunteering|Geocaching|Metal detecting|Crosswords|Shell collecting|Camping|"
  "Journaling|Rock collecting|Tie-dye|Candle making|Making jewelry|Model rockets";

static const char HOLIDAYS[] =
  "Christmas|New Year's Eve|Halloween|Easter|Thanksgiving|Valentine's Day|Hanukkah|Diwali|Lunar New Year|Eid|"
  "Carnival|Mardi Gras|Independence Day|Mother's Day|Father's Day|April Fools' Day|St Patrick's Day|"
  "Summer vacation|Spring break|Fireworks|Santa Claus|Reindeer|Snowman|Christmas tree|Presents|Stocking|"
  "Jack-o'-lantern|Trick or treat|Easter egg|Easter bunny|Roast turkey|Candles|Wreath|Mistletoe|"
  "Gingerbread house|Advent calendar|Parade|Picnic|Road trip|Ugly sweater|Sleigh|Day of the Dead|"
  "Lantern festival|Kwanzaa|Holi|Groundhog Day|Boxing Day|Earth Day|Anniversary|Red envelope|Menorah|Bonfire|"
  "Birthday";

static const char TOWN[] =
  "Bank|Post office|Supermarket|Bakery|Library|Hospital|Police station|Fire station|Cinema|Museum|Park|"
  "Playground|Restaurant|Cafe|Hotel|Pharmacy|Train station|Bus stop|Airport|Gas station|Parking lot|"
  "Traffic light|Crosswalk|Bridge|Fountain|Statue|Market|Shopping mall|Gym|Swimming pool|Zoo|Stadium|"
  "Town hall|Skyscraper|Apartment|Bookstore|Toy store|Barber shop|Laundromat|Pet shop|Flower shop|Bench|"
  "Streetlight|Sidewalk|Roundabout|Ice cream truck|Food truck|Billboard|Car wash|Pizzeria|Bowling alley|"
  "Arcade|Theater|Aquarium|Factory|Construction site|Fire hydrant|Bike lane|Recycling bin";

static const char SPACE[] =
  "Sun|Moon|Earth|Mars|Venus|Jupiter|Saturn|Mercury|Neptune|Pluto|Star|Comet|Asteroid|Meteor|Galaxy|"
  "Black hole|Rocket|Astronaut|Alien|UFO|Satellite|Space station|Telescope|Planet|Orbit|Gravity|Milky Way|"
  "Nebula|Eclipse|Constellation|Big Dipper|Crater|Solar system|Spacesuit|Moon landing|Mars rover|"
  "Shooting star|Supernova|Launch pad|Mission control|Zero gravity|Spaceship|Light year|Big Bang|Sunspot|"
  "Spacewalk|Meteor shower|Space shuttle|Moon base|Asteroid belt|Wormhole|Dwarf planet|Observatory|"
  "Planetarium|Space food|Countdown|Solar flare";

static const char FANTASY[] =
  "Dragon|Unicorn|Wizard|Witch|Fairy|Elf|Dwarf|Giant|Troll|Goblin|Ogre|Mermaid|Vampire|Werewolf|Ghost|Zombie|"
  "Phoenix|Griffin|Centaur|Pegasus|Minotaur|Cyclops|Kraken|Sphinx|Genie|Magic wand|Spell book|Potion|"
  "Crystal ball|Castle|Knight|Princess|Prince|King|Queen|Treasure map|Sword|Shield|Crown|Flying carpet|"
  "Magic lamp|Cauldron|Broomstick|Gnome|Leprechaun|Yeti|Sea serpent|Hydra|Enchanted forest|Curse|Magic portal|"
  "Dungeon|Throne|Tooth fairy|Time machine|Orc|Pixie|Wishing well|Magic mirror|Bigfoot";

static const char TOYS[] =
  "Teddy bear|Doll|Ball|Kite|Yo-yo|Building blocks|Toy car|Train set|Board game|Chess|Checkers|Dominoes|"
  "Playing cards|Dice|Marbles|Jump rope|Spinning top|Rocking horse|Slide|Swing|Seesaw|Trampoline|Sandbox|"
  "Water gun|Soap bubbles|Action figure|Puppet|Dollhouse|Robot|Remote control car|Hopscotch|Hide and seek|Tag|"
  "Musical chairs|Tug of war|Treasure hunt|Rubber duck|Jack-in-the-box|Kaleidoscope|Piggy bank|Stickers|"
  "Snow globe|Pinball|Bingo|Tic-tac-toe|Rock paper scissors|Charades|Simon says|Water balloon|Stuffed animal|"
  "Pogo stick|Model plane|Paper airplane|Toy box|Rattle|Pinwheel";

static const char TOOLS[] =
  "Hammer|Screwdriver|Wrench|Saw|Drill|Pliers|Tape measure|Nail|Screw|Ladder|Shovel|Rake|Wheelbarrow|Axe|"
  "Chisel|Sandpaper|Paintbrush|Paint roller|Spirit level|Toolbox|Flashlight|Glue gun|Duct tape|Garden hose|"
  "Watering can|Lawn mower|Chainsaw|Crowbar|Mallet|Hacksaw|Utility knife|Clamp|Pickaxe|Sledgehammer|Anvil|"
  "Magnifying glass|Pitchfork|Trowel|Bolt|Hinge|Soldering iron|Extension cord|Padlock|Rope|Funnel|"
  "Garden shears|Hex key|Chain|Scraper|Workbench|Safety goggles|Hard hat|Tool belt|Leaf blower|Pocket knife|"
  "Car jack";

static const char SCIENCE[] =
  "Atom|Magnet|Microscope|Test tube|Laboratory|Experiment|Electricity|Battery|Light bulb|DNA|Cell|Bacteria|"
  "Virus|Fossil|Dinosaur|Thermometer|Periodic table|Oxygen|Hydrogen|Carbon|Water cycle|Photosynthesis|"
  "Evolution|Energy|Solar panel|Wind turbine|Circuit|Lab coat|Beaker|Chemical reaction|Formula|Laser|Prism|"
  "Gears|Pulley|Lever|Crystal|Static electricity|Molecule|Microchip|Germs|Vaccine|X-ray|Ecosystem|Climate|"
  "Recycling|Petri dish|Bunsen burner|Slime|Inventor|Hypothesis|Electron|Computer|Acid|Steam engine|"
  "Sound wave|Friction";

static const char PARTY[] =
  "Balloons|Confetti|Party hat|Streamers|Pinata|DJ|Dance floor|Disco ball|Invitation|Gift bag|Wrapping paper|"
  "Ribbon|Birthday card|Photo booth|Costume party|Pool party|Sleepover|Pizza party|Surprise party|Guest list|"
  "Snacks|Potato chips|Candy|Party favors|Garland|Banner|Fairy lights|Glow sticks|Bouncy castle|Face paint|"
  "Musical statues|Limbo|Conga line|Slow dance|Speaker|Microphone|Playlist|Group photo|Thank you card|"
  "Birthday song|Party popper|Paper plates|Masquerade|Dress code|Barbecue|Dance battle|Bubble machine|"
  "Game night|Movie night|Paper lanterns|Chocolate fountain";

static const char TECH[] =
  "Phone|Laptop|Tablet|Headphones|Smartwatch|Selfie|Emoji|Password|Text message|Video call|Hashtag|Meme|"
  "Podcast|Livestream|Notification|Screenshot|Download|Website|Email|Spam|Ringtone|Group chat|"
  "Profile picture|Username|Touchscreen|App|Game console|Drone|Virtual reality|Smart speaker|Printer|Webcam|"
  "Online shopping|Streaming|Autocorrect|Unboxing video|Tutorial|Robot vacuum|3D printer|Power bank|"
  "Computer mouse|Search engine|Hard drive|Cloud storage|Satellite dish|GPS|Voice assistant|Smart TV|Pixel|"
  "Loading screen|Software update|Earbuds|Selfie stick|Phone charger|Dark mode";

const category_t categories[N_CATS] = {
  {"Food", FOOD}, {"Fruits", FRUITS}, {"Vegetables", VEGETABLES}, {"Desserts", DESSERTS}, {"Drinks", DRINKS},
  {"Kitchen", KITCHEN}, {"Animals", ANIMALS}, {"The sea", SEA}, {"Countries", COUNTRIES}, {"Cities", CITIES},
  {"Famous places", PLACES}, {"Jobs", JOBS}, {"Sports", SPORTS}, {"School", SCHOOL}, {"At home", HOME},
  {"Clothes", CLOTHES}, {"Transport", TRANSPORT}, {"Instruments", INSTRUMENTS}, {"Nature and weather", NATURE},
  {"The body", BODY}, {"Hobbies", HOBBIES}, {"Holidays", HOLIDAYS}, {"In town", TOWN}, {"Space", SPACE},
  {"Fantasy", FANTASY}, {"Toys and games", TOYS}, {"Tools", TOOLS}, {"Science", SCIENCE}, {"Party", PARTY},
  {"Technology", TECH},
};

int word_count(int cat) {
  const char *s = categories[cat].words;
  int n = 1;
  while (*s) n += *s++ == '|';
  return n;
}

int word_get(int cat, int i, char *out, int cap) {
  const char *s = categories[cat].words;
  while (i > 0 && *s) i -= *s++ == '|';
  int n = 0;
  while (s[n] && s[n] != '|' && n < cap - 1) out[n] = s[n], n++;
  out[n] = 0;
  return n;
}
