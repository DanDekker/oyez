#include "catalog.h"

static const CatalogEntry k_catalog[] = {
    /* 1. GLOBAL & MULTILATERAL */
    {"UN News - Global Perspective", "https://news.un.org/feed/subscribe/en/news/all/rss.xml", "https://news.un.org", "geopolitics", REGION_GLOBAL, "UN", "Multilateral", "en", "official", "United Nations dispatch, peace operations & humanitarian affairs"},
    {"World Health Organization (WHO)", "https://www.who.int/rss-feeds/news-english.xml", "https://www.who.int", "health", REGION_GLOBAL, "UN", "Multilateral", "en", "official", "Global epidemiology, disease outbreaks & public health directives"},
    {"Rest of World", "https://restofworld.org/feed/latest/", "https://restofworld.org", "technology", REGION_GLOBAL, "INT", "Global South", "en", "specialist", "Global technology impact outside Western markets"},
    {"ICIJ - International Consortium of Investigative Journalists", "https://www.icij.org/feed/", "https://www.icij.org", "law", REGION_GLOBAL, "INT", "Cross-Border", "en", "independent", "Cross-border investigative journalism, financial secrecy & corruption"},
    {"OCCRP - Organized Crime and Corruption Reporting Project", "https://www.occrp.org/en/rss", "https://www.occrp.org", "law", REGION_GLOBAL, "INT", "Cross-Border", "en", "independent", "Global investigative reporting on organized crime and state capture"},
    {"Bellingcat", "https://www.bellingcat.com/feed/", "https://www.bellingcat.com", "security", REGION_GLOBAL, "INT", "Open Source Intelligence", "en", "independent", "OSINT investigations, conflict verification & digital forensics"},
    {"The New Humanitarian", "https://www.thenewhumanitarian.org/rss/all.xml", "https://www.thenewhumanitarian.org", "human-rights", REGION_GLOBAL, "INT", "Humanitarian", "en", "independent", "Field reporting from humanitarian crises, displacement & conflict zones"},
    {"Global Voices", "https://globalvoices.org/feed/", "https://globalvoices.org", "human-rights", REGION_GLOBAL, "INT", "Transnational", "en", "independent", "Citizen media, digital rights & vernacular perspectives worldwide"},
    {"Foreign Policy", "https://foreignpolicy.com/feed/", "https://foreignpolicy.com", "geopolitics", REGION_GLOBAL, "US", "Global Statecraft", "en", "specialist", "Diplomatic strategy, grand strategy & international security"},
    {"Crisis Group - International Crisis Group", "https://www.crisisgroup.org/rss.xml", "https://www.crisisgroup.org", "defense", REGION_GLOBAL, "INT", "Conflict Prevention", "en", "specialist", "Early warning conflict analysis and diplomatic resolution"},

    /* 2. NORTH AMERICA */
    {"NPR - National & World News", "https://feeds.npr.org/1001/rss.xml", "https://www.npr.org", "public-affairs", REGION_NORTH_AMERICA, "US", "United States", "en", "public-broadcaster", "US public broadcasting, national policy & international coverage"},
    {"NPR - World Desk", "https://feeds.npr.org/1004/rss.xml", "https://www.npr.org/sections/world/", "geopolitics", REGION_NORTH_AMERICA, "US", "United States", "en", "public-broadcaster", "International bureaus and foreign affairs"},
    {"PBS NewsHour - World", "https://www.pbs.org/newshour/feeds/rss/world", "https://www.pbs.org/newshour", "geopolitics", REGION_NORTH_AMERICA, "US", "United States", "en", "public-broadcaster", "US public television foreign affairs and diplomatic analysis"},
    {"ProPublica", "https://www.propublica.org/feeds/propublica/main", "https://www.propublica.org", "law", REGION_NORTH_AMERICA, "US", "United States", "en", "independent", "Public-interest investigative journalism and accountability reporting"},
    {"The Intercept", "https://theintercept.com/feed/?lang=en", "https://theintercept.com", "security", REGION_NORTH_AMERICA, "US", "United States", "en", "independent", "National security, civil liberties & investigative journalism"},
    {"Just Security", "https://www.justsecurity.org/feed/", "https://www.justsecurity.org", "law", REGION_NORTH_AMERICA, "US", "United States", "en", "academic", "Law, rights, and national security analysis"},
    {"Lawfare", "https://www.lawfaremedia.org/rss.xml", "https://www.lawfaremedia.org", "law", REGION_NORTH_AMERICA, "US", "United States", "en", "academic", "Hard national security choices, cyber law & constitutional governance"},
    {"War on the Rocks", "https://warontherocks.com/feed/", "https://warontherocks.com", "defense", REGION_NORTH_AMERICA, "US", "United States", "en", "specialist", "Defense strategy, military operations & foreign policy"},
    {"CBC News - World", "https://www.cbc.ca/webfeed/rss/rss-world", "https://www.cbc.ca/news/world", "geopolitics", REGION_NORTH_AMERICA, "CA", "Canada", "en", "public-broadcaster", "Canadian public broadcasting on international and Arctic affairs"},
    {"CBC News - Canada Top Stories", "https://www.cbc.ca/webfeed/rss/rss-topstories", "https://www.cbc.ca/news", "politics", REGION_NORTH_AMERICA, "CA", "Canada", "en", "public-broadcaster", "Canadian federal politics, economy & society"},
    {"The Narwhal", "https://thenarwhal.ca/feed/", "https://thenarwhal.ca", "climate", REGION_NORTH_AMERICA, "CA", "Canada", "en", "independent", "Investigative environmental journalism across Canada"},
    {"Mexico News Daily", "https://mexiconewsdaily.com/feed/", "https://mexiconewsdaily.com", "politics", REGION_NORTH_AMERICA, "MX", "Mexico", "en", "national-record", "Mexican economy, trade, politics & regional security"},

    /* 3. LATIN AMERICA & CARIBBEAN */
    {"MercoPress - South Atlantic News Agency", "https://en.mercopress.com/rss/", "https://en.mercopress.com", "geopolitics", REGION_LATIN_AMERICA, "UY", "Southern Cone", "en", "wire", "Mercosur, Southern Cone, Falklands & South Atlantic geopolitics"},
    {"Buenos Aires Times", "https://www.batimes.com.ar/feed", "https://www.batimes.com.ar", "economy", REGION_LATIN_AMERICA, "AR", "Southern Cone", "en", "national-record", "Argentine macroeconomics, politics & Southern Cone affairs"},
    {"Agencia Brasil (EBC)", "https://agenciabrasil.ebc.com.br/rss/ultimasnoticias/feed.xml", "https://agenciabrasil.ebc.com.br", "politics", REGION_LATIN_AMERICA, "BR", "Brazil", "pt", "public-broadcaster", "Brazilian public news agency covering federal policy, Amazonia & BRICS"},
    {"The Brazilian Report", "https://brazilian.report/feed/", "https://brazilian.report", "politics", REGION_LATIN_AMERICA, "BR", "Brazil", "en", "independent", "In-depth analysis of Brazilian politics, judiciary & economy"},
    {"Amazonia Real", "https://amazoniareal.com.br/feed/", "https://amazoniareal.com.br", "climate", REGION_LATIN_AMERICA, "BR", "Amazon Basin", "pt", "independent", "Indigenous rights, deforestation & Amazon Basin investigative journalism"},
    {"Colombia Reports", "https://colombiareports.com/feed/", "https://colombiareports.com", "politics", REGION_LATIN_AMERICA, "CO", "Andean", "en", "independent", "Colombian peace process, security, politics & Andean affairs"},
    {"La Silla Vacia", "https://www.lasillavacia.com/feed/", "https://www.lasillavacia.com", "politics", REGION_LATIN_AMERICA, "CO", "Andean", "es", "independent", "Colombian power networks, political accountability & policy"},
    {"CIPER Chile", "https://www.ciperchile.cl/feed/", "https://www.ciperchile.cl", "law", REGION_LATIN_AMERICA, "CL", "Southern Cone", "es", "independent", "Chilean investigative reporting on public institutions and finance"},
    {"IDL-Reporteros", "https://www.idl-reporteros.pe/feed/", "https://www.idl-reporteros.pe", "law", REGION_LATIN_AMERICA, "PE", "Andean", "es", "independent", "Peruvian investigative journalism on corruption and justice"},
    {"El Faro", "https://elfaro.net/es/rss.xml", "https://elfaro.net", "human-rights", REGION_LATIN_AMERICA, "SV", "Central America", "es", "independent", "Central American investigative reporting, migration & governance"},
    {"Caracas Chronicles", "https://www.caracaschronicles.com/feed/", "https://www.caracaschronicles.com", "politics", REGION_LATIN_AMERICA, "VE", "Andean / Caribbean", "en", "independent", "Venezuelan political economy, energy & humanitarian crisis"},
    {"Havana Times", "https://havanatimes.org/feed/", "https://havanatimes.org", "society", REGION_LATIN_AMERICA, "CU", "Caribbean", "en", "independent", "Independent journalism from Cuba and the Caribbean"},

    /* 4. EUROPE */
    {"BBC News - World", "https://feeds.bbci.co.uk/news/world/rss.xml", "https://www.bbc.com/news/world", "geopolitics", REGION_EUROPE, "GB", "Western Europe", "en", "public-broadcaster", "Global news coverage from BBC World Service bureaus"},
    {"BBC News - Europe", "https://feeds.bbci.co.uk/news/world/europe/rss.xml", "https://www.bbc.com/news/world/europe", "geopolitics", REGION_EUROPE, "GB", "Western Europe", "en", "public-broadcaster", "European diplomacy, security & domestic affairs"},
    {"The Guardian - World News", "https://www.theguardian.com/world/rss", "https://www.theguardian.com/world", "geopolitics", REGION_EUROPE, "GB", "Western Europe", "en", "national-record", "International reporting, climate & investigative dispatches"},
    {"Deutsche Welle (DW) - Top Stories", "https://rss.dw.com/xml/rss-en-top", "https://www.dw.com", "geopolitics", REGION_EUROPE, "DE", "Central Europe", "en", "public-broadcaster", "German international broadcaster covering EU, Eastern Europe & global affairs"},
    {"Der Spiegel International", "https://www.spiegel.de/international/index.rss", "https://www.spiegel.de/international/", "geopolitics", REGION_EUROPE, "DE", "Central Europe", "en", "national-record", "German and European investigative and political journalism"},
    {"France 24 - International", "https://www.france24.com/en/rss", "https://www.france24.com/en/", "geopolitics", REGION_EUROPE, "FR", "Western Europe", "en", "public-broadcaster", "French international broadcasting on Europe, Africa & Middle East"},
    {"Le Monde in English", "https://www.lemonde.fr/en/rss/une.xml", "https://www.lemonde.fr/en/", "geopolitics", REGION_EUROPE, "FR", "Western Europe", "en", "national-record", "French paper of record covering European diplomacy, politics & culture"},
    {"Euronews - International", "https://www.euronews.com/rss", "https://www.euronews.com", "politics", REGION_EUROPE, "EU", "European Union", "en", "public-broadcaster", "Pan-European news, European Commission & Parliament policy"},
    {"EUobserver", "https://euobserver.com/rss.xml", "https://euobserver.com", "politics", REGION_EUROPE, "BE", "European Union", "en", "specialist", "Brussels policymaking, EU foreign policy, digital regulation & climate"},
    {"Swissinfo - International Service of SBC", "https://www.swissinfo.ch/eng/rss", "https://www.swissinfo.ch/eng", "economy", REGION_EUROPE, "CH", "Central Europe", "en", "public-broadcaster", "Multilateral Geneva, global banking, neutrality & science"},
    {"El Pais English", "https://feeds.elpais.com/mrss-s/pages/ep/site/english.elpais.com/portada", "https://english.elpais.com", "politics", REGION_EUROPE, "ES", "Southern Europe", "en", "national-record", "Spanish & Ibero-American politics, society & Mediterranean affairs"},
    {"ANSA English", "https://www.ansa.it/english/news/english_nr_rss.xml", "https://www.ansa.it/english/", "politics", REGION_EUROPE, "IT", "Southern Europe", "en", "wire", "Italian wire service covering Rome, Mediterranean & EU affairs"},
    {"RTE News - Ireland & World", "https://www.rte.ie/feeds/rss/?index=/news/", "https://www.rte.ie/news/", "politics", REGION_EUROPE, "IE", "Western Europe", "en", "public-broadcaster", "Irish public broadcaster covering Ireland, UK-EU relations & tech regulation"},
    {"Notes from Poland", "https://notesfrompoland.com/feed/", "https://notesfrompoland.com", "politics", REGION_EUROPE, "PL", "Central Europe", "en", "independent", "Polish politics, rule of law, NATO eastern flank & economy"},
    {"Balkan Insight (BIRN)", "https://balkaninsight.com/feed/", "https://balkaninsight.com", "geopolitics", REGION_EUROPE, "RS", "Southeast Europe", "en", "independent", "Balkan Investigative Reporting Network covering Southeast & Central Europe"},
    {"The Kyiv Independent", "https://kyivindependent.com/feed/", "https://kyivindependent.com", "defense", REGION_EUROPE, "UA", "Eastern Europe", "en", "independent", "Ukrainian front-line reporting, defense, reconstruction & governance"},
    {"Ukrinform - Ukrainian National News Agency", "https://www.ukrinform.net/rss/block-lastnews", "https://www.ukrinform.net", "defense", REGION_EUROPE, "UA", "Eastern Europe", "en", "wire", "Ukrainian state wire service on security, Black Sea & diplomacy"},
    {"Meduza (English)", "https://meduza.io/rss/en/all", "https://meduza.io/en", "politics", REGION_EUROPE, "LV", "Eastern Europe", "en", "independent", "Independent Russian-in-exile investigative reporting on Russia & Eurasia"},
    {"The Moscow Times", "https://www.themoscowtimes.com/rss/news", "https://www.themoscowtimes.com", "geopolitics", REGION_EUROPE, "NL", "Eastern Europe", "en", "independent", "Independent reporting on Russian politics, sanctions, energy & regions"},
    {"The Barents Observer", "https://thebarentsobserver.com/en/rss.xml", "https://thebarentsobserver.com", "geopolitics", REGION_EUROPE, "NO", "Nordic / Arctic", "en", "independent", "High North, Arctic security, nuclear fleet & climate change"},

    /* 5. MIDDLE EAST & NORTH AFRICA */
    {"Al Jazeera English", "https://www.aljazeera.com/xml/rss/all.xml", "https://www.aljazeera.com", "geopolitics", REGION_MIDDLE_EAST, "QA", "Persian Gulf", "en", "public-broadcaster", "Middle East, North Africa & Global South 24-hour news network"},
    {"Middle East Eye", "https://www.middleeasteye.net/rss", "https://www.middleeasteye.net", "geopolitics", REGION_MIDDLE_EAST, "GB", "Levant & Gulf", "en", "independent", "Levant, Gulf, North Africa & Turkey political and security reporting"},
    {"Al-Monitor", "https://www.al-monitor.com/rss", "https://www.al-monitor.com", "geopolitics", REGION_MIDDLE_EAST, "US", "Middle East", "en", "specialist", "Middle East diplomacy, energy corridors & regional security"},
    {" +972 Magazine", "https://www.972mag.com/feed/", "https://www.972mag.com", "human-rights", REGION_MIDDLE_EAST, "IL", "Levant", "en", "independent", "Independent Israeli-Palestinian on-the-ground journalism"},
    {"The Times of Israel", "https://www.timesofisrael.com/feed/", "https://www.timesofisrael.com", "politics", REGION_MIDDLE_EAST, "IL", "Levant", "en", "national-record", "Israeli politics, defense, judiciary & regional diplomacy"},
    {"L'Orient Today", "https://today.lorientlejour.com/rss", "https://today.lorientlejour.com", "politics", REGION_MIDDLE_EAST, "LB", "Levant", "en", "national-record", "Lebanese politics, economy, banking crisis & Levant security"},
    {"Syria Direct", "https://syriadirect.org/feed/", "https://syriadirect.org", "human-rights", REGION_MIDDLE_EAST, "SY", "Levant", "en", "independent", "Local Syrian journalism on displacement, governance & reconstruction"},
    {"Rudaw English", "https://www.rudaw.net/english/rss", "https://www.rudaw.net/english", "geopolitics", REGION_MIDDLE_EAST, "IQ", "Mesopotamia / Kurdistan", "en", "regional-network", "Iraq, Kurdistan Region, Syria, Iran & Turkey energy and security"},
    {"Iran International", "https://www.iranintl.com/en/feed", "https://www.iranintl.com/en", "geopolitics", REGION_MIDDLE_EAST, "IR", "Persian Gulf", "en", "independent", "Iranian domestic politics, nuclear program, human rights & regional proxies"},
    {"Bianet English", "https://bianet.org/rss/english", "https://bianet.org/english", "human-rights", REGION_MIDDLE_EAST, "TR", "Anatolia", "en", "independent", "Independent Turkish journalism on press freedom, labor & Kurdish affairs"},
    {"Mada Masr", "https://www.madamasr.com/en/feed/", "https://www.madamasr.com/en/", "politics", REGION_MIDDLE_EAST, "EG", "North Africa", "en", "independent", "Independent Egyptian investigative journalism, Red Sea & Nile politics"},

    /* 6. SUB-SAHARAN & CONTINENTAL AFRICA */
    {"AllAfrica - Pan-African News", "https://allafrica.com/tools/headlines/rdf/latest/headlines.rdf", "https://allafrica.com", "geopolitics", REGION_AFRICA, "ZA", "Pan-African", "en", "wire", "Pan-African news aggregation from 130+ African media organizations"},
    {"Africanews", "https://www.africanews.com/feed/rss", "https://www.africanews.com", "geopolitics", REGION_AFRICA, "CG", "Pan-African", "en", "regional-network", "Multilingual pan-African news covering African Union, ECOWAS & SADC"},
    {"The Continent / Mail & Guardian", "https://mg.co.za/feed/", "https://mg.co.za", "politics", REGION_AFRICA, "ZA", "Southern Africa", "en", "national-record", "South African & continental investigative journalism and governance"},
    {"Daily Maverick", "https://www.dailymaverick.co.za/dmrss/", "https://www.dailymaverick.co.za", "law", REGION_AFRICA, "ZA", "Southern Africa", "en", "independent", "South African investigative journalism, energy transition & state accountability"},
    {"GroundUp News", "https://groundup.org.za/feeds/all/rss.xml", "https://groundup.org.za", "human-rights", REGION_AFRICA, "ZA", "Southern Africa", "en", "independent", "Public interest journalism on housing, labor, health & social justice in South Africa"},
    {"Premium Times Nigeria", "https://www.premiumtimesng.com/feed", "https://www.premiumtimesng.com", "politics", REGION_AFRICA, "NG", "West Africa", "en", "independent", "Nigerian investigative journalism, ECOWAS politics, oil & security"},
    {"HumAngle Media", "https://humanglemedia.com/feed/", "https://humanglemedia.com", "security", REGION_AFRICA, "NG", "West Africa / Sahel", "en", "specialist", "Lake Chad Basin, Sahel insurgency, conflict & humanitarian security"},
    {"The EastAfrican", "https://www.theeastafrican.co.ke/tea/rss", "https://www.theeastafrican.co.ke", "economy", REGION_AFRICA, "KE", "East Africa", "en", "regional-network", "East African Community (Kenya, Tanzania, Uganda, Rwanda, DRC) trade & diplomacy"},
    {"Addis Standard", "https://addisstandard.com/feed/", "https://addisstandard.com", "geopolitics", REGION_AFRICA, "ET", "Horn of Africa", "en", "independent", "Ethiopia, Horn of Africa, Red Sea geopolitics & African Union headquarters"},
    {"Radio Dabanga - Sudan", "https://www.dabangasudan.org/en/feed", "https://www.dabangasudan.org/en", "human-rights", REGION_AFRICA, "SD", "Horn of Africa", "en", "independent", "Sudan conflict, Darfur humanitarian crisis & transitional politics"},

    /* 7. SOUTH ASIA */
    {"The Hindu - National & International", "https://www.thehindu.com/news/feeder/default.rss", "https://www.thehindu.com", "politics", REGION_SOUTH_ASIA, "IN", "India", "en", "national-record", "Indian constitutional politics, foreign policy, economy & Supreme Court"},
    {"The Indian Express", "https://indianexpress.com/feed/", "https://indianexpress.com", "politics", REGION_SOUTH_ASIA, "IN", "India", "en", "national-record", "Indian investigative journalism, policy explainers & governance"},
    {"The Wire (India)", "https://thewire.in/feed", "https://thewire.in", "politics", REGION_SOUTH_ASIA, "IN", "India", "en", "independent", "Independent Indian reporting on civil liberties, economy, science & diplomacy"},
    {"Scroll.in", "https://feeds.feedburner.com/ScrollinArticles.rss", "https://scroll.in", "society", REGION_SOUTH_ASIA, "IN", "India", "en", "independent", "Indian politics, culture, climate policy & technology"},
    {"Dawn - Pakistan", "https://www.dawn.com/feeds/home", "https://www.dawn.com", "politics", REGION_SOUTH_ASIA, "PK", "Pakistan", "en", "national-record", "Pakistan paper of record covering federal politics, IMF macroeconomics & security"},
    {"The Daily Star - Bangladesh", "https://www.thedailystar.net/frontpage/rss.xml", "https://www.thedailystar.net", "politics", REGION_SOUTH_ASIA, "BD", "Bengal Delta", "en", "national-record", "Bangladesh governance, garment trade, Bay of Bengal geopolitics & climate"},
    {"The Kathmandu Post", "https://kathmandupost.com/rss", "https://kathmandupost.com", "politics", REGION_SOUTH_ASIA, "NP", "Himalayas", "en", "national-record", "Nepali federal politics, Himalayan climate change & India-China diplomacy"},
    {"Himal Southasian", "https://www.himalmag.com/feed/", "https://www.himalmag.com", "geopolitics", REGION_SOUTH_ASIA, "LK", "Cross-Border South Asia", "en", "independent", "Trans-regional South Asian politics, culture & cross-border analysis"},

    /* 8. EAST ASIA */
    {"NHK World Japan", "https://www3.nhk.or.jp/rss/news/cat0.xml", "https://www3.nhk.or.jp/nhkworld/", "politics", REGION_EAST_ASIA, "JP", "Japan", "en", "public-broadcaster", "Japanese public broadcaster covering East Asia, technology, seismic safety & economy"},
    {"The Japan Times", "https://www.japantimes.co.jp/feed/", "https://www.japantimes.co.jp", "geopolitics", REGION_EAST_ASIA, "JP", "Japan", "en", "national-record", "Japanese defense policy, Bank of Japan monetary policy & Indo-Pacific security"},
    {"Yonhap News Agency", "https://en.yna.co.kr/RSS/news.xml", "https://en.yna.co.kr", "geopolitics", REGION_EAST_ASIA, "KR", "Korean Peninsula", "en", "wire", "South Korean wire service on Korean Peninsula security, semiconductors & trade"},
    {"The Korea Herald", "https://www.koreaherald.com/common/rss_xml.php?ct=102", "https://www.koreaherald.com", "economy", REGION_EAST_ASIA, "KR", "Korean Peninsula", "en", "national-record", "South Korean business, chaebols, chip supply chain & diplomacy"},
    {"Daily NK English", "https://www.dailynk.com/english/feed/", "https://www.dailynk.com/english/", "security", REGION_EAST_ASIA, "KR", "Korean Peninsula", "en", "specialist", "Inside-source reporting on North Korean markets, military & society"},
    {"Focus Taiwan (CNA English)", "https://focustaiwan.tw/RSS", "https://focustaiwan.tw", "geopolitics", REGION_EAST_ASIA, "TW", "Taiwan Strait", "en", "wire", "Taiwan Central News Agency on cross-strait security, TSMC & semiconductor ecosystem"},
    {"Hong Kong Free Press (HKFP)", "https://hongkongfp.com/feed/", "https://hongkongfp.com", "law", REGION_EAST_ASIA, "HK", "Greater China", "en", "independent", "Independent English-language reporting from Hong Kong and Greater China"},
    {"South China Morning Post (SCMP)", "https://www.scmp.com/rss/91/feed", "https://www.scmp.com", "geopolitics", REGION_EAST_ASIA, "HK", "Greater China", "en", "national-record", "China economy, industrial policy, tech regulation & US-China relations"},
    {"Sixth Tone", "https://www.sixthtone.com/rss", "https://www.sixthtone.com", "society", REGION_EAST_ASIA, "CN", "Mainland China", "en", "specialist", "Contemporary Chinese society, demographics, urban policy & environment"},

    /* 9. SOUTHEAST ASIA */
    {"Channel NewsAsia (CNA)", "https://www.channelnewsasia.com/api/v1/rss-outbound-feed?_format=xml", "https://www.channelnewsasia.com", "geopolitics", REGION_SOUTHEAST_ASIA, "SG", "Maritime Southeast Asia", "en", "public-broadcaster", "Singapore & ASEAN regional politics, maritime security & Asian markets"},
    {"The Straits Times - Asia", "https://www.straitstimes.com/news/asia/rss.xml", "https://www.straitstimes.com", "geopolitics", REGION_SOUTHEAST_ASIA, "SG", "Maritime Southeast Asia", "en", "national-record", "Southeast & East Asian diplomacy, trade corridors & ASEAN summits"},
    {"Rappler", "https://www.rappler.com/feed/", "https://www.rappler.com", "politics", REGION_SOUTHEAST_ASIA, "PH", "Maritime Southeast Asia", "en", "independent", "Philippine investigative journalism, West Philippine Sea / South China Sea & governance"},
    {"Tempo English (Indonesia)", "https://en.tempo.co/rss/terkini", "https://en.tempo.co", "politics", REGION_SOUTHEAST_ASIA, "ID", "Maritime Southeast Asia", "en", "independent", "Indonesian investigative reporting, nickel supply chain, Nusantara & ASEAN"},
    {"The Jakarta Post", "https://www.thejakartapost.com/rss", "https://www.thejakartapost.com", "economy", REGION_SOUTHEAST_ASIA, "ID", "Maritime Southeast Asia", "en", "national-record", "Indonesian economy, archipelago politics & Southeast Asian affairs"},
    {"Prachatai English", "https://prachataienglish.com/rss.xml", "https://prachataienglish.com", "human-rights", REGION_SOUTHEAST_ASIA, "TH", "Mekong", "en", "independent", "Independent Thai journalism on constitutional reform, labor & Mekong human rights"},
    {"The Irrawaddy", "https://www.irrawaddy.com/feed", "https://www.irrawaddy.com", "defense", REGION_SOUTHEAST_ASIA, "MM", "Mekong", "en", "independent", "Independent Myanmar conflict reporting, ethnic resistance & borderland security"},
    {"VnExpress International", "https://e.vnexpress.net/rss/news.rss", "https://e.vnexpress.net", "economy", REGION_SOUTHEAST_ASIA, "VN", "Mekong", "en", "national-record", "Vietnamese manufacturing, global supply chains, energy & diplomacy"},
    {"Malaysiakini", "https://www.malaysiakini.com/rss/en/news.rss", "https://www.malaysiakini.com", "politics", REGION_SOUTHEAST_ASIA, "MY", "Maritime Southeast Asia", "en", "independent", "Independent Malaysian reporting on federal politics, judiciary & anti-corruption"},

    /* 10. CENTRAL ASIA & CAUCASUS */
    {"Eurasianet", "https://eurasianet.org/rss.xml", "https://eurasianet.org", "geopolitics", REGION_CENTRAL_ASIA, "KZ", "Central Asia & Caucasus", "en", "specialist", "Kazakhstan, Uzbekistan, Kyrgyzstan, Tajikistan, Turkmenistan & South Caucasus"},
    {"The Astana Times", "https://astanatimes.com/feed/", "https://astanatimes.com", "economy", REGION_CENTRAL_ASIA, "KZ", "Central Asia", "en", "national-record", "Kazakhstan Middle Corridor trade, energy, uranium & Central Asian diplomacy"},
    {"Times of Central Asia", "https://timesca.com/feed/", "https://timesca.com", "geopolitics", REGION_CENTRAL_ASIA, "KG", "Central Asia", "en", "regional-network", "Pan-Central Asian economy, water security, Belt and Road & regional integration"},
    {"OC Media - Open Caucasus Media", "https://oc-media.org/feed/", "https://oc-media.org", "human-rights", REGION_CENTRAL_ASIA, "GE", "South Caucasus", "en", "independent", "Independent reporting from Georgia, Armenia, Azerbaijan & North Caucasus"},
    {"Civil Georgia", "https://civil.ge/feed", "https://civil.ge", "politics", REGION_CENTRAL_ASIA, "GE", "South Caucasus", "en", "independent", "Georgian politics, EU accession, Black Sea security & South Caucasus diplomacy"},

    /* 11. OCEANIA & PACIFIC ISLANDS */
    {"ABC News Australia - Just In", "https://www.abc.net.au/news/feed/51120/rss.xml", "https://www.abc.net.au/news", "politics", REGION_OCEANIA, "AU", "Australasia", "en", "public-broadcaster", "Australian Broadcasting Corporation national and Indo-Pacific coverage"},
    {"Radio New Zealand (RNZ) - World & Pacific", "https://www.rnz.co.nz/rss/world.xml", "https://www.rnz.co.nz", "geopolitics", REGION_OCEANIA, "NZ", "Australasia", "en", "public-broadcaster", "New Zealand public broadcaster with deep South Pacific bureau coverage"},
    {"RNZ Pacific", "https://www.rnz.co.nz/rss/pacific.xml", "https://www.rnz.co.nz/international/pacific-news", "climate", REGION_OCEANIA, "NZ", "Pacific Islands", "en", "public-broadcaster", "Polynesia, Melanesia & Micronesia governance, climate resilience & ocean security"},
    {"Islands Business", "https://islandsbusiness.com/feed/", "https://islandsbusiness.com", "geopolitics", REGION_OCEANIA, "FJ", "Pacific Islands", "en", "regional-network", "Pacific Islands Forum, Fiji, Papua New Guinea, Solomon Islands & ocean economy"},

    /* 12. SCIENCE, TECHNOLOGY, CYBER & ECONOMICS */
    {"Hacker News (Frontpage)", "https://hnrss.org/frontpage", "https://news.ycombinator.com", "technology", REGION_GLOBAL, "INT", "Global Tech", "en", "specialist", "Systems engineering, open source, startups & computer science"},
    {"Ars Technica", "https://feeds.arstechnica.com/arstechnica/index", "https://arstechnica.com", "technology", REGION_NORTH_AMERICA, "US", "United States", "en", "specialist", "Deep hardware architecture, kernel engineering, spaceflight & tech policy"},
    {"LWN.net", "https://lwn.net/headlines/rss", "https://lwn.net", "technology", REGION_GLOBAL, "INT", "Open Source", "en", "specialist", "Linux kernel development, compilers, security & free software governance"},
    {"Krebs on Security", "https://krebsonsecurity.com/feed/", "https://krebsonsecurity.com", "cyber", REGION_GLOBAL, "US", "Cybersecurity", "en", "independent", "Investigative reporting on cybercrime, zero-days & threat intelligence"},
    {"Schneier on Security", "https://www.schneier.com/feed/atom/", "https://www.schneier.com", "cyber", REGION_GLOBAL, "US", "Cryptography & Policy", "en", "academic", "Applied cryptography, AI security & surveillance policy"},
    {"Nature - Latest Research & News", "https://www.nature.com/nature.rss", "https://www.nature.com", "science", REGION_GLOBAL, "GB", "Global Science", "en", "academic", "Peer-reviewed breakthroughs across physics, biology, climate & materials"},
    {"Quanta Magazine", "https://www.quantamagazine.org/feed/", "https://www.quantamagazine.org", "science", REGION_GLOBAL, "US", "Fundamental Science", "en", "academic", "Mathematics, theoretical physics, theoretical computer science & biology"},
    {"Phys.org", "https://phys.org/rss-feed/", "https://phys.org", "science", REGION_GLOBAL, "INT", "Global Science", "en", "specialist", "Daily research dispatches from global universities and national laboratories"},
    {"Carbon Brief", "https://www.carbonbrief.org/feed/", "https://www.carbonbrief.org", "climate", REGION_GLOBAL, "GB", "Climate Science", "en", "specialist", "Data-driven climate science, energy policy & emissions modeling"},
};

const CatalogEntry *catalog_entries(size_t *out_count) {
  if(out_count) *out_count = sizeof(k_catalog) / sizeof(k_catalog[0]);
  return k_catalog;
}
