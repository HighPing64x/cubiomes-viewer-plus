// Verifies that a session file keeps the Bedrock edition flag, and that older
// session files without the edition line still load as Java worlds.
//
// Note: the session header stores the Minecraft version and the large-biomes
// flag (WorldInfo::write); the seed and the y level are part of the normal
// settings and are not written here, so the comparison below is limited to the
// fields that the header actually carries.
#include "../src/config.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QTextStream>

#include <cstdio>

int getStructureConfig_override(int stype, int mc, StructureConfig *sconf)
{
    return getStructureConfig(stype, mc, sconf);
}

static QByteArray headerText(const WorldInfo& wi)
{
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    QTextStream ts(&buf);
    WorldInfo copy = wi;
    copy.write(ts);
    ts.flush();
    return data;
}

static void parseHeader(const QByteArray& data, WorldInfo *out)
{
    QBuffer buf(const_cast<QByteArray*>(&data));
    buf.open(QIODevice::ReadOnly);
    QTextStream ts(&buf);
    while (!ts.atEnd())
        out->read(ts.readLine());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int fails = 0;

    const int bedrocks[] = {BE_UNDEF, BE_1_18, BE_1_19, BE_1_20, BE_1_21};
    const int javas[] = {MC_1_21_3, MC_1_18_2, MC_1_19_4, MC_1_20_6, MC_1_21_3};

    for (int i = 0; i < 5; i++)
    {
        WorldInfo wi;
        wi.mc = javas[i];
        wi.bedrock = bedrocks[i];
        wi.seed = 123456789ULL;
        wi.large = false;
        wi.y = 63;

        QByteArray data = headerText(wi);
        WorldInfo back;
        back.reset();
        parseHeader(data, &back);

        bool same = back.mc == wi.mc && back.large == wi.large
                 && back.bedrock == wi.bedrock;

        if (!same)
        {
            printf("FAIL roundtrip: bedrock %d -> %d, mc %d -> %d\n",
                wi.bedrock, back.bedrock, wi.mc, back.mc);
            fails++;
        }
        else
        {
            printf("ok roundtrip bedrock=%d (%s)\n", back.bedrock,
                back.isBedrock() ? "bedrock" : "java");
        }

        // The edition line must be present in the file itself.
        QByteArray expect = QByteArray("#Bedrock:  ")
            + QByteArray::number(wi.bedrock) + "\n";
        if (!data.contains(expect))
        {
            printf("FAIL: header misses '%s' (got '%s')\n",
                expect.constData(), data.constData());
            fails++;
        }
    }

    // A session file written by an older version has no edition line.
    {
        QByteArray legacy("#MC:       1.21.3\n#Large:    0\n");
        WorldInfo back;
        back.reset();
        parseHeader(legacy, &back);

        if (back.bedrock != BE_UNDEF || back.mc != MC_1_21_3 || back.isBedrock())
        {
            printf("FAIL legacy session parsed as bedrock=%d mc=%d\n",
                back.bedrock, back.mc);
            fails++;
        }
        else
        {
            printf("ok legacy session stays a Java world\n");
        }
    }

    // An out of range edition value must fall back to Java instead of
    // leaving an unusable world behind.
    {
        QByteArray broken("#MC:       1.21.3\n#Bedrock:  99\n");
        WorldInfo back;
        back.reset();
        parseHeader(broken, &back);
        if (back.bedrock != BE_UNDEF)
        {
            printf("FAIL invalid edition accepted: %d\n", back.bedrock);
            fails++;
        }
        else
        {
            printf("ok invalid edition falls back to Java\n");
        }
    }

    printf("%s\n", fails ? "SESSION CHECKS FAILED" : "ALL SESSION CHECKS PASSED");
    return fails ? 1 : 0;
}
