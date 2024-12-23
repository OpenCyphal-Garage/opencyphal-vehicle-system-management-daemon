//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include <ocvsmd/sdk/daemon.hpp>

int main(const int argc, const char** const argv)
{
    (void) argc;
    (void) argv;

    if (auto daemon = ocvsmd::sdk::Daemon::make())
    {
        daemon->send_messages();
    }

    return 0;
}
