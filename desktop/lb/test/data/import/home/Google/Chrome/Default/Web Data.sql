-- A made-up Chrome Web Data file: built into "Web Data" by the import demo.
CREATE TABLE keywords (id INTEGER PRIMARY KEY, short_name VARCHAR, keyword VARCHAR, url VARCHAR,
                       safe_for_autoreplace INTEGER, prepopulate_id INTEGER,
                       created_by_policy INTEGER, is_active INTEGER, starter_pack_id INTEGER);
INSERT INTO keywords VALUES (1, 'Wikipedia', 'w',
    'https://en.wikipedia.org/w/index.php?search={searchTerms}', 0, 0, 0, 1, 0);
INSERT INTO keywords VALUES (2, 'Google', 'google.com',
    'https://www.google.com/search?q={searchTerms}', 0, 1, 0, 0, 0);
CREATE TABLE local_addresses_type_tokens (guid VARCHAR, type INTEGER, value VARCHAR,
                                          verification_status INTEGER);
INSERT INTO local_addresses_type_tokens VALUES ('a', 7, 'Ana Lee', 0);
INSERT INTO local_addresses_type_tokens VALUES ('a', 77, '1 Rua Example', 0);
INSERT INTO local_addresses_type_tokens VALUES ('a', 33, 'Porto', 0);
INSERT INTO local_addresses_type_tokens VALUES ('a', 36, 'PT', 0);
