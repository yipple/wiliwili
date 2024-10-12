//
// Created by fang on 2022/7/7.
//

#include <utility>
#include <borealis/core/thread.hpp>
#include <pystring.h>

#include "fragment/home_hots_history.hpp"
#include "view/video_card.hpp"
#include "view/recycling_grid.hpp"
#include "utils/image_helper.hpp"
#include "utils/activity_helper.hpp"
#include "utils/config_helper.hpp"

using namespace brls::literals;

class DataSourceHotsHistoryVideoList : public RecyclingGridDataSource {
public:
    explicit DataSourceHotsHistoryVideoList(bilibili::HotsHistoryVideoListResult result)
        : videoList(std::move(result)) {
        auto it = videoList.begin();
        while (it != videoList.end()) {
            if (ProgramConfig::instance().HasBanUser(it->owner.mid)) {
                it = videoList.erase(it);  // 删除元素并更新迭代器

                brls::Logger::info("ERROS {} {}", it->owner.mid, it->owner.name);
            } else {
                ++it;  // 继续下一个元素
            }
        }
    }

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        //从缓存列表中取出 或者 新生成一个表单项
        RecyclingGridItemVideoCard* item = (RecyclingGridItemVideoCard*)recycler->dequeueReusableCell("Cell");

        bilibili::HotsHistoryVideoResult& r = this->videoList[index];
        std::string h_ext = ImageHelper::h_ext;
        if (pystring::endswith(r.pic, ".gif")) {
            // gif 图片暂时按照 jpg 来解析
            h_ext = pystring::replace(h_ext, ".webp", ".jpg");
        }
        item->setCard(r.pic + h_ext, r.title, r.owner.name, r.pubdate, r.stat.view, r.stat.danmaku, r.duration);
        item->setAchievement(r.achievement);
        return item;
    }

    size_t getItemCount() override { return videoList.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override { Intent::openBV(videoList[index].bvid); }

    void appendData(const bilibili::HotsHistoryVideoListResult& data) {
        for (const auto& i : data) {
            if (ProgramConfig::instance().HasBanUser(i.owner.mid)) {
                brls::Logger::info("Baned {} {}", i.owner.name, i.owner.mid);
                continue;
            }
            this->videoList.emplace_back(i);
        }
    }

    void clearData() override { this->videoList.clear(); }

private:
    bilibili::HotsHistoryVideoListResult videoList;
};

HomeHotsHistory::HomeHotsHistory() {
    this->inflateFromXMLRes("xml/fragment/home_hots_history.xml");
    brls::Logger::debug("Fragment HomeHotsHistory: create");
    this->recyclingGrid->registerCell("Cell", []() { return RecyclingGridItemVideoCard::create(); });
    this->recyclingGrid->setRefreshAction([this]() {
        AutoTabFrame::focus2Sidebar(this);
        this->recyclingGrid->showSkeleton();
        this->requestData();
    });
    this->requestData();
}

void HomeHotsHistory::onCreate() {
    this->registerTabAction("wiliwili/home/common/refresh"_i18n, brls::ControllerButton::BUTTON_X,
                            [this](brls::View* view) -> bool {
                                this->recyclingGrid->refresh();
                                return true;
                            });
}

brls::View* HomeHotsHistory::create() { return new HomeHotsHistory(); }

void HomeHotsHistory::onHotsHistoryList(const bilibili::HotsHistoryVideoListResult& result,
                                        const std::string& explain) {
    brls::Threading::sync([this, result, explain]() {
        recyclingGrid->setDataSource(new DataSourceHotsHistoryVideoList(result));
        this->labelExplain->setText(explain);
    });
}

HomeHotsHistory::~HomeHotsHistory() { brls::Logger::debug("Fragment HomeHotsHistoryActivity: delete"); }

void HomeHotsHistory::onError(const std::string& error) {
    brls::sync([this, error]() { this->recyclingGrid->setError(error); });
}