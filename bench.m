string = fileread('bench.jsonl');
now = time();
a = ndjson_load_string(string, 'threading', 'single', 'mode', 'relaxed');
duration = time() - now;
printf("ndjson_load_string (single thread): %ds\n", duration);

now = time();
a = ndjson_load_string(string, 'threading', 'multi', 'mode', 'relaxed');
duration = time() - now;
printf("ndjson_load_string (multi thread): %ds\n", duration);

now = time();
a = ndjson_load_file('bench.jsonl', 'threading', 'single', 'mode', 'relaxed');
duration = time() - now;
printf("ndjson_load_file (single thread): %ds\n", duration);

now = time();
a = ndjson_load_file('bench.jsonl', 'threading', 'multi', 'mode', 'relaxed');
duration = time() - now;
printf("ndjson_load_file (multi thread): %ds\n", duration);

string = fileread("bench.json");
now = time();
a = load_json(string);
duration = time() - now;
printf("load_json: %ds\n", duration);

now = time();
a = jsondecode(string);
duration = time() - now;
printf("jsondecode: %ds\n", duration);
