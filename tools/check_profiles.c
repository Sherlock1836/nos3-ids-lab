/* Offline checks for the same C detector linked into the cFS app. */
#include <assert.h>
#include <stdio.h>
#include "ids_detector.h"
#include "ids_model.h"
#include "ids_profiles.h"

static double HighScore(const float *features)
{
    (void)features;
    return 0.9;
}

static void CheckFixtures(void)
{
    const IDS_Command commands[] = {{0, 8}, {4, 12}};
    unsigned expected;
    assert(IDS_CheckContract(commands, 2, 4, 12, &expected) == IDS_ACCEPT);
    assert(IDS_CheckContract(commands, 2, 4, 8, &expected) == IDS_WRONG_SIZE && expected == 12);
    assert(IDS_CheckContract(commands, 2, 7, 8, &expected) == IDS_UNKNOWN_CODE);

    IDS_History a = {0};
    IDS_History b = {0};
    float values[IDS_FEATURE_COUNT];
    assert(!IDS_ObserveTiming(&a, 0, 16383, 8, 0, values));
    assert(!IDS_ObserveTiming(&b, 50, 125, 8, 0, values));
    assert(IDS_ObserveTiming(&a, 200, 0, 8, 0, values));
    assert(values[0] == 1 && values[1] == 200 && values[4] == 2);
    assert(IDS_ObserveTiming(&b, 300, 126, 8, 0, values));
    assert(values[1] == 250 && values[4] == 2);
    assert(!IDS_ObserveTiming(&a, 5200, 1, 8, 0, values));
    assert(!IDS_ObserveTiming(&b, 100, 127, 8, 0, values));

    const IDS_Profile uncalibrated = {0};
    const IDS_Profile profile = {HighScore, 0.7, 180, 3, 5};
    double score;
    values[1] = 199;
    values[4] = 4;
    assert(IDS_CheckTiming(&uncalibrated, values, &score) == IDS_TIMING_UNCALIBRATED);
    assert(IDS_CheckTiming(&profile, values, &score) == IDS_TIMING_PASS);
    values[1] = 100;
    assert(IDS_CheckTiming(&profile, values, &score) == IDS_TIMING_OUTLIER);
    values[4] = 6;
    assert(IDS_CheckTiming(&profile, values, &score) == IDS_TIMING_RATE);

    // Abstract dense timing observations exercise both deployed profiles.
    const IDS_Profile *deployed[] = {&IDS_SampleProfile, &IDS_CiLabProfile};
    for (size_t i = 0; i < sizeof(deployed) / sizeof(deployed[0]); ++i)
    {
        const IDS_Profile *profile = deployed[i];
        IDS_History history = {0};
        assert(!IDS_ObserveTiming(&history, 0, 0, 8, 0, values));
        for (unsigned n = 1; n <= profile->rate_ceiling; ++n)
        {
            assert(IDS_ObserveTiming(&history, n * 20, (uint16_t)n, 8, 0, values));
        }
        assert(IDS_CheckTiming(profile, values, &score) == IDS_TIMING_RATE);
    }
}

int main(void)
{
    CheckFixtures();
    unsigned mid;
    float values[IDS_FEATURE_COUNT];
    while (scanf("%x,%f,%f,%f,%f,%f", &mid, &values[0], &values[1], &values[2], &values[3], &values[4]) == 6)
    {
        const IDS_Profile *profile = mid == 0x18FA ? &IDS_SampleProfile : &IDS_CiLabProfile;
        assert(mid == 0x18FA || mid == 0x18E0);
        double score = 0;
        IDS_TimingResult result = IDS_CheckTiming(profile, values, &score);
        printf("%.17g,%s,%u\n", score, IDS_TimingName(result), (unsigned)(IDS_ModelScore(values) > IDS_MODEL_THRESHOLD));
    }
    return feof(stdin) ? 0 : 1;
}
