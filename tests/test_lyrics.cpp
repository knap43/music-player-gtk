#include "Lyrics.h"
#include "check.h"

TEST(plainText)
{
    const Lyrics l = Lyrics::parse("\n\nFirst line\nSecond line\n\n\n\nThird\n\n");
    CHECK(!l.isSynced());
    CHECK_EQ(l.lines().size(), 4u); // blank runs collapse to one gap
    CHECK_EQ(l.lines().front().text, std::string("First line"));
    CHECK_EQ(l.lines().back().text, std::string("Third"));
    CHECK_EQ(l.lineAt(5000), -1);
}

TEST(syncedWithHeadersAndRepeats)
{
    const Lyrics l = Lyrics::parse("[ar:Someone]\n[ti:Song]\n"
                                   "[00:10.50]Second\n"
                                   "[00:01.00][01:00.00]Chorus\n"
                                   "[00:20.123]Third <00:21.00>word\n");
    CHECK(l.isSynced());
    CHECK_EQ(l.lines().size(), 4u);
    CHECK_EQ(l.lines()[0].timeMs, 1000);
    CHECK_EQ(l.lines()[0].text, std::string("Chorus"));
    CHECK_EQ(l.lines()[1].timeMs, 10500);
    CHECK_EQ(l.lines()[2].timeMs, 20123);
    CHECK_EQ(l.lines()[2].text, std::string("Third word"));
    CHECK_EQ(l.lines()[3].timeMs, 60000);
    CHECK_EQ(l.lineAt(0), -1);
    CHECK_EQ(l.lineAt(1000), 0);
    CHECK_EQ(l.lineAt(10499), 0);
    CHECK_EQ(l.lineAt(10500), 1);
    CHECK_EQ(l.lineAt(999999), 3);
}

TEST(offsetShiftsEarlier)
{
    const Lyrics l = Lyrics::parse("[offset:+500]\n[00:02.00]Line\n");
    CHECK_EQ(l.lines().front().timeMs, 1500);
}

TEST(bomAndCrLf)
{
    const Lyrics l = Lyrics::parse("\xEF\xBB\xBF[00:01.0]A\r\n[00:02.0]B\r\n");
    CHECK(l.isSynced());
    CHECK_EQ(l.lines().size(), 2u);
    CHECK_EQ(l.lines()[0].timeMs, 1000);
}

TEST(emptyInput)
{
    CHECK(Lyrics::parse("").isEmpty());
    CHECK(Lyrics::parse("\n  \n[ar:Only a header]\n").isEmpty());
}

int main()
{
    return check::run();
}
