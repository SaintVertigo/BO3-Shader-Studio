#pragma once

#include <QString>

namespace bo3
{

struct PackageRegressionSummary
{
    int passed = 0;
    int failed = 0;
    QString output;
};

PackageRegressionSummary runPackageRegressionSuite();

} // namespace bo3
