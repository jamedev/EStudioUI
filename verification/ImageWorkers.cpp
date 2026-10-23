#include <EStudioUI/Image.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    const auto shared = EStudioUI::Image::Load(argv[1]);
    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
    std::atomic<bool> failed = false;
    std::mutex mutex;
    std::vector<std::uint64_t> identities;
    std::array<std::thread, 8> workers;
    for (auto& worker : workers)
    {
        worker = std::thread(
            [&]
            {
                try
                {
                    for (int iteration = 0; iteration < 100; ++iteration)
                    {
                        try
                        {
                            (void)EStudioUI::Image::Decode(std::span(bytes).first(20));
                            failed = true;
                        }
                        catch (const std::exception&)
                        {
                        }
                        const auto image = EStudioUI::Image::Decode(bytes);
                        if (image.GetWidth() != 4 || image.GetHeight() != 2 || image.GetPixels().back() != 128 ||
                            shared.GetPixels()[0] != 255)
                            failed = true;
                        std::lock_guard lock(mutex);
                        identities.push_back(image.GetIdentity());
                    }
                }
                catch (...)
                {
                    failed = true;
                }
            });
    }
    for (auto& worker : workers)
        worker.join();
    std::sort(identities.begin(), identities.end());
    if (failed || identities.size() != 800 ||
        std::adjacent_find(identities.begin(), identities.end()) != identities.end())
        return 1;
    std::cout << "Actual Image.cpp/ImageDecoder.cpp: 800 concurrent decodes, malformed-input recovery, shared reads "
                 "and unique identities passed.\n";
}
