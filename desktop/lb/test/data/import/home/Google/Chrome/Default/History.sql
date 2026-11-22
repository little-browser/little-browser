-- A made-up Chrome History file: built into "History" by the import demo, which moves every
-- visit but the one titled Old to yesterday.
CREATE TABLE urls (id INTEGER PRIMARY KEY, url LONGVARCHAR, title LONGVARCHAR,
                   visit_count INTEGER, typed_count INTEGER, last_visit_time INTEGER,
                   hidden INTEGER);
INSERT INTO urls VALUES (1, 'https://news.example/', 'News', 4, 1, 13348540800000000, 0);
INSERT INTO urls VALUES (2, 'https://weather.example/', 'Weather', 1, 0, 13348540800000000, 0);
INSERT INTO urls VALUES (3, 'chrome://settings/', 'Settings', 1, 0, 13348540800000000, 0);
INSERT INTO urls VALUES (4, 'https://old.example/', 'Old', 9, 0, 13348540800000000, 0);
