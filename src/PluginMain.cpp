#include <iostream>
#include <string>
#include <sstream>
#include <deque>
#include <cxxopts.hpp>

#include <MHTR/Plugin/IPlugin.h>
#include <MHTR/Plugin/Export.h>
#include <MHTR/Synther/FileOperations.h>
#include <MHTR/Synther/Cxx/Header.h>
#include <MHTR/Synther/LineGroup.h>
#include <MHTR/Synther/MultiLineGroup.h>
#include <MHTR/Synther/NamespaceBlock.h>
#include <MHTR/Synther/MultiLineSingleLine.h>
#include <MHTR/Synther/Cxx/Utility.h>
#include <MHTR/Metadata/Synthers.h>
#include <MHTR/Metadata/Utility.h>
#include <MHTR/Storage.h>
#include <MHTR/Utility.h>
#include <MHTR/Json/FileOp.h>
#include <MHTR/Json/Provider.h>
#include <MHTR/Json/Ref.h>
#include <MHTR/Json/FromFileProvider.h>
#include <MHTR/Random/NumberGenerator.h>
#include <OMHTR/SDK.h>

using namespace MHTR;
using namespace OMHTR;

template<typename JsonSynther>
class JsonAccessSynther : public ILineSynthesizer {
public:
    JsonAccessSynther(const std::string& objectName, const std::string& key, const std::string& type, bool bWrite = false)
        : mObjectName(objectName)
        , mKey(key)
        , mType(type)
        , mbWrite(bWrite)
    {}

    JsonAccessSynther(const std::string& objectName, ILineSynthesizer* key, const std::string& type, bool bWrite = false)
        : JsonAccessSynther(objectName, key->Synth(), type, bWrite)
    {}

    std::string Synth() const override
    {
        return JsonSynther::AccessSynth(mObjectName, mKey, mType, mbWrite);
    }

    std::string mObjectName;
    std::string mKey;
    std::string mType;
    bool mbWrite;
};

class AssignSynther : public ILineSynthesizer {
public:
    AssignSynther(ILineSynthesizer* lhs, ILineSynthesizer* rhs)
        : mLhs(lhs)
        , mRhs(rhs)
    {}

    std::string Synth() const override
    {
        return mLhs->Synth() + " = " + mRhs->Synth();
    }

    ILineSynthesizer* mLhs;
    ILineSynthesizer* mRhs;
};

class XoredSynther : public ILineSynthesizer {
public:
    XoredSynther(ILineSynthesizer* xoring, std::string key)
        : mXoring(xoring)
        , mKey(key)
    {}

    std::string Synth() const override
    {
        return mXoring->Synth() + " ^ " + mKey;
    }

    ILineSynthesizer* mXoring;
    std::string mKey;
};

class NlohmannJson {
public:
    using Synther = typename JsonAccessSynther<NlohmannJson>;

    static std::string GetType()
    {
        return "nlohmann::json";
    }

    static std::string GetTypeInc()
    {
        return "nlohmann/json.hpp";
    }

    static std::string AccessSynth(const std::string& objectName, const std::string& key, const std::string& type, bool bWrite = false) {
        if (bWrite)
            return objectName + "[" + key + "]";

        return objectName + "[" + key + "].get<" + type + ">()";
    }
};

class JsonCpp {
public:
    using Synther = typename JsonAccessSynther<JsonCpp>;

    static std::string GetType()
    {
        return "Json::Value";
    }

    static std::string GetTypeInc()
    {
        return "json/value.h";
    }

    static std::string AccessSynth(const std::string& objectName, const std::string& key, const std::string& type, bool bWrite = false) {
        if (bWrite)
            return objectName + "[" + key + "]";

        return objectName + "[" + key + "].as<" + type + ">()";
    }
};

class IJsonServiceProvider {
public:
    virtual ~IJsonServiceProvider() = default;
    virtual std::string GetValueType() = 0;
    virtual std::string GetTypeInclude() = 0;
    virtual std::string AccessSynth(const std::string& objectName, const std::string& key, const std::string& type, bool bWrite = false) = 0;
};

template<typename Json>
class JsonServiceProvider : public IJsonServiceProvider {
public:
    using JsonSynther = typename Json::Synther;

    std::string GetValueType() override
    {
        return Json::GetType();
    }

    std::string GetTypeInclude() override
    {
        return Json::GetTypeInc();
    }

    std::string AccessSynth(const std::string& objectName, const std::string& key, const std::string& type, bool bWrite = false) override
    {
        return JsonSynther(objectName, key, type, bWrite).Synth();
    }
};

class FromJsonTypeJsonServiceProviderFactory {
public:
    std::unique_ptr<IJsonServiceProvider> Create(const std::string& jsonType)
    {
        if (jsonType == "jsoncpp")
            return std::make_unique<JsonServiceProvider<JsonCpp>>();

        if (jsonType == "nlohmannjson")
            return std::make_unique<JsonServiceProvider<NlohmannJson>>();

        throw std::runtime_error(std::string() + "Unsupported JSON type " + "'" + jsonType + "'");
    }
};

class IMetadataSaltProvider {
public:
    virtual ~IMetadataSaltProvider() = default;
    virtual uint64_t GetSalt() = 0;
};

class IMetadataEncKeyProvider {
public:
    virtual ~IMetadataEncKeyProvider() = default;
    virtual uint64_t GetEncKey() = 0;
};

class IMetadataPageProvider : public IMetadataSaltProvider, public IMetadataEncKeyProvider {
public:
    virtual ~IMetadataPageProvider() = default;
};

class FromJsonMetadataPageProvider : public IMetadataPageProvider {
public:
    FromJsonMetadataPageProvider(const nlohmann::json& page)
        : mPage(page)
    {}

    uint64_t GetSalt() override
    {
        return mPage["salt"].get<uint64_t>();
    }

    uint64_t GetEncKey() override
    {
        return mPage["encKey"].get<uint64_t>();
    }

    nlohmann::json mPage;
};

class IMetadataPageProviderFactory {
public:
    virtual ~IMetadataPageProviderFactory() = default;
    virtual std::unique_ptr<IMetadataPageProvider> Create(const MetadataTarget* target) = 0;
};

class IMetadataUidTransform {
public:
    virtual ~IMetadataUidTransform() = default;
    virtual std::string Transform(const MetadataTarget* target, const std::string& fullUid) = 0;
};

class DefaultMetadataUidTransform : public IMetadataUidTransform {
public:
    std::string Transform(const MetadataTarget* target, const std::string& fullUid) override
    {
        return fullUid;
    }
};

class SaltMetadataUidTransform : public IMetadataUidTransform {
public:
    SaltMetadataUidTransform(IMetadataPageProviderFactory* pageProviderFactory)
        : mPageProviderFactory(pageProviderFactory)
    {}

    std::string Transform(const MetadataTarget* target, const std::string& fullUid) override
    {
        std::unique_ptr<IMetadataPageProvider> page = mPageProviderFactory->Create(target);

        return fullUid + "_" + (std::stringstream() << std::hex << page->GetSalt()).str();
    }

    IMetadataPageProviderFactory* mPageProviderFactory;
};

class HashMetadataUidTransform : public IMetadataUidTransform {
public:
    std::string Transform(const MetadataTarget* target, const std::string& fullUid) override
    {
        return std::to_string(OMHTR::fnv1_32_hash(fullUid.data(), fullUid.size()));
    }
};

class MetadataUidTransformPipeline : public IMetadataUidTransform {
public:
    MetadataUidTransformPipeline(const std::vector<IMetadataUidTransform*>& stages)
        : mStages(stages)
    {}

    std::string Transform(const MetadataTarget* target, const std::string& fullUid) override
    {
        std::string uidDigest = fullUid;

        for (const auto& stage : mStages)
            uidDigest = stage->Transform(target, uidDigest);

        return uidDigest;
    }

    std::vector<IMetadataUidTransform*> mStages;
};

class FromJsonMetadataBookMetadataPageProviderFactory : public IMetadataPageProviderFactory {
public:
    FromJsonMetadataBookMetadataPageProviderFactory(IJsonProvider* bookProvider)
        : mBook(*(bookProvider->GetJson()))
    {}

    FromJsonMetadataBookMetadataPageProviderFactory(const nlohmann::json& book)
        : mBook(book)
    {}

    std::unique_ptr<IMetadataPageProvider> Create(const MetadataTarget* target) override
    {
        return std::make_unique<FromJsonMetadataPageProvider>(mBook[target->GetFullName()]);
    }

    nlohmann::json mBook;
};

nlohmann::json BookRandomCreate(const MetadataTargetSet& targets)
{
    nlohmann::json book;
    auto& rng = HighEntropyRNG::Instance();

    for (const MetadataTarget* target : targets)
    {
        nlohmann::json page;

        page["salt"] = rng.gen<uint64_t>();
        page["encKey"] = rng.gen<uint64_t>();

        book[target->GetFullName()] = std::move(page);
    }

    return book;
}

std::string ToUpperCase(const std::string& input) {
    std::string result = input;
    for (char& c : result) {
        c = std::toupper(static_cast<unsigned char>(c));
    }
    return result;
}

class HPPJsonWriter : public IPlugin {
public:
    HPPJsonWriter()
    {}

	MHTRPLUGIN_METADATA("OMHTR", "Obfuscated MHTR Report Writer")

	void OnCLIRegister(cxxopts::Options& options) override
	{
        options.add_options()
            ("rohppjson", "Obfuscated Hpp using JSON and MHTRSDK", cxxopts::value<std::string>())
            ("omhtr-json", "Obfuscated Json output", cxxopts::value<std::string>())
            ("omhtr-ns", "Report Namespace to Use", cxxopts::value<std::string>()->default_value(""))
            ("omhtr-jsonimpl", "Json Implementation to Use", cxxopts::value<std::string>()->default_value("nlohmannjson"))
            ("omhtr-book", "Path to metadata book containing hash/salt information for Metadatas", cxxopts::value<std::string>()->default_value(""))
            ("omhtr-book-rotate", "Refresh Keys/Salts from Metadata Page in the Metadata Book", cxxopts::value<bool>()->default_value("false"))
            ("omhtr-salt", "Apply Salting Operation to Unique Identifiers", cxxopts::value<bool>()->default_value("false"))
            ("omhtr-hash", "Apply Hashing Operation to Unique Identifiers", cxxopts::value<bool>()->default_value("false"))
            ("omhtr-encrypt", "Apply Encryption Operation to Metadata Offsets", cxxopts::value<bool>()->default_value("false"));
	}

	void OnCLIParsed(cxxopts::ParseResult& parseRes) override
	{
		if (!parseRes.count("rohppjson"))
			return;

        mHppOutputPath = parseRes["rohppjson"].as<std::string>();
        mMetadataBookPath = parseRes["omhtr-book"].as<std::string>();
        mJsonType = parseRes["omhtr-jsonimpl"].as<std::string>();
        mNamespace = parseRes["omhtr-ns"].as<std::string>();
        mbBookRotate = parseRes.count("omhtr-book-rotate") > 0;
        mbApplySalt = parseRes.count("omhtr-salt") > 0;
        mbApplyHash = parseRes.count("omhtr-hash") > 0;
        mbApplyEnc = parseRes.count("omhtr-encrypt") > 0;
        mbBookExist = std::filesystem::exists(mMetadataBookPath);

        if (parseRes.count("omhtr-json"))
            mJsonOutputPath = parseRes["omhtr-json"].as<std::string>();

        if (mJsonOutputPath.empty())
            mJsonOutputPath = std::filesystem::path(mHppOutputPath).stem().string() + ".json";

        JsonServiceProviderInitialize();
	}

    void JsonServiceProviderInitialize()
    {
        try {
            mJsonServiceProvider = FromJsonTypeJsonServiceProviderFactory().Create(mJsonType);
        }
        catch (const std::exception& e)
        {
            std::cout << e.what() << ", Defaulting to 'nlohmannjson'.\n";
            mJsonServiceProvider = FromJsonTypeJsonServiceProviderFactory().Create(mJsonType = "nlohmannjson");
        }
    }
    
    void MetadataUIDTFSInitialize()
    {
        if (!mMetadataUidTfs.empty())
            return;

        mMetadataUidTfs.push_back(
            &mDefaultUidTf
        );

        if (mbApplySalt)
            mMetadataUidTfs.push_back(
                (IMetadataUidTransform*)mMetadataUidTfStorage.Store(
                    std::make_unique<SaltMetadataUidTransform>(mBookPageProviderFactory.get())
                ).get()
            );

        if (mbApplyHash)
            mMetadataUidTfs.push_back(
                (IMetadataUidTransform*)mMetadataUidTfStorage.Store(
                    std::make_unique<HashMetadataUidTransform>()
                ).get()
            );
    }

    void OnResult(const MetadataTargetSet& result) override
    {
        if (mHppOutputPath.empty() || result.empty())
                return;

        if (mNamespace.empty())
            mNamespace = FromMultiMetadataTargetPseudoNs(result);

        BookInitialize(result);
        MetadataUIDTFSInitialize();
        WriteHpp(result);
        WriteJson(result);
        BookPersist();
    }

    void BookNormalize(const MetadataTargetSet& targets)
    {
        nlohmann::json randBook = BookRandomCreate(targets);
        nlohmann::json& book = mBookPageProviderFactory->mBook;

        for (const MetadataTarget* target : targets)
        {
            if (book.contains(target->GetFullName()))
                continue;

            book[target->GetFullName()] = randBook[target->GetFullName()];
        }
    }

    void BookInitialize(const MetadataTargetSet& result)
    {
        if (mBookPageProviderFactory)
            return;

        if (mbBookRotate || mMetadataBookPath.empty() || !mbBookExist)
        {
            mBookPageProviderFactory = std::make_unique<FromJsonMetadataBookMetadataPageProviderFactory>(BookRandomCreate(result));
            return;
        }

        FromFileJsonProvider book(mMetadataBookPath);
        mBookPageProviderFactory = std::make_unique<FromJsonMetadataBookMetadataPageProviderFactory>(&book);
        BookNormalize(result);
    }

    void BookPersist()
    {
        if (mMetadataBookPath.empty())
            return;

        nlohmann::json& book = mBookPageProviderFactory->mBook;
        FileWrite(mMetadataBookPath, book);
    }

    void WriteHpp(const MetadataTargetSet& result)
    {
        CxxHeaderHead headerHead; headerHead.GetIncBlockBuilder()
            ->Add("MHTRSDK.h")
            ->Add(mJsonServiceProvider->GetTypeInclude());

        Line argLine(LiteralT(mJsonServiceProvider->GetValueType(), true, true) + " json");
        MultiNsMultiMetadataSynther multiFn(result, [&](const std::string& ns, const MetadataTargetSet& targets, Indent) {
            MultiLineSingleLine metadataProviderDef(Line("MHTR::MetadataMap result;"));
            MultiLineSingleLine metadataProviderRet(Line("return result;"));
            auto lines = Transform<std::vector<Line>>(targets.begin(), targets.end(), [&](const MetadataTarget* target) {
                bool currIsPattern = std::holds_alternative<PatternMetadata>(target->mResult.mMetadata);
                std::string type = currIsPattern ? "std::string" : "uint64_t";
                std::string fullUid = Literal(
                    GetMetadataUidTransformer().Transform(
                        target, target->GetFullName()
                    )
                );
                Line lval("result[" + fullUid + "]");
                Line rval(mJsonServiceProvider->AccessSynth(
                    "json",
                    fullUid,
                    type,
                    false
                ));
                
                if (!currIsPattern && mbApplyEnc)  rval = Line(
                    XoredSynther(
                        &rval,
                        "0x" + (
                            std::stringstream() << std::hex <<
                            mBookPageProviderFactory
                            ->Create(target)
                            ->GetEncKey()
                            ).str() + "ull"
                    ).Synth()
                );

                return AssignSynther(&lval, &rval).Synth() + ";";
                });
            LineSynthesizerGroup lineGroup(AsMultiInterface<ILineSynthesizer>(lines));
            MultiLineSynthesizerGroup fnContent({
                &metadataProviderDef,
                &lineGroup,
                &metadataProviderRet
                });
            return MetadataProviderFunction(ns + "Create", &fnContent, &argLine).Synth();
            });

        std::deque<Line> constExprIdentifiers;

        if (mbApplySalt || mbApplyHash)
        {
            constExprIdentifiers = Transform<std::deque<Line>>(result.begin(), result.end(), [&](const MetadataTarget* target) {
                std::string fullName = ToUpperCase(ReplaceAllOcurrencies(target->GetFullName(), "::", "_")) + "_UID";

                Line lval("constexpr auto " + fullName);
                Line rval(Literal(GetMetadataUidTransformer().Transform(
                    target, target->GetFullName()
                )));

                return AssignSynther(&lval, &rval).Synth() + ";";
                });

            constExprIdentifiers.emplace_front(Line::mEmpty);
            constExprIdentifiers.emplace_back(Line::mEmpty);
        }

        LineSynthesizerGroup constExprIdentifierIfaces(AsMultiInterface<ILineSynthesizer>(constExprIdentifiers));

        Line metadataProviderforwardArg("json");
        MetadataProviderMergerFunction multiProviderMerger(result, &argLine, &metadataProviderforwardArg);
        MultiLineSynthesizerGroup multiFnAndMerger({
            &multiFn,
            &MultiLineSingleLine::mEmptyLine,
            &multiProviderMerger
            });
        NamespaceBlock nsBlock(&multiFnAndMerger, mNamespace);
        MultiLineSynthesizerGroup fullHpp({
            &headerHead,
            &constExprIdentifierIfaces,
            &nsBlock
            });

        FileWrite(mHppOutputPath, &fullHpp);
    }

    void WriteJson(const MetadataTargetSet& result)
    {
        if (mJsonOutputPath.empty())
            return;

        nlohmann::json fullJson;

        std::for_each(result.begin(), result.end(), [&](const MetadataTarget* target) {
            std::visit([&](const auto& metadata) {
                std::string key = GetMetadataUidTransformer().Transform(
                    target, target->GetFullName()
                );

                if constexpr (std::is_same_v<uint64_t, decltype(metadata.mValue)>)
                    fullJson[key] = metadata.mValue ^ (mbApplyEnc ? mBookPageProviderFactory->Create(target)->GetEncKey() : 0);
                else
                    fullJson[key] = metadata.mValue;
                }, target->mResult.mMetadata);
            });

        FileWrite(mJsonOutputPath, fullJson);
    }

    MetadataUidTransformPipeline GetMetadataUidTransformer()
    {
        return MetadataUidTransformPipeline(mMetadataUidTfs);
    }

	std::string mHppOutputPath;
    std::string mJsonOutputPath;
    std::string mMetadataBookPath;
    std::string mNamespace;
    std::string mJsonType;
    std::unique_ptr<FromJsonMetadataBookMetadataPageProviderFactory> mBookPageProviderFactory;
    bool mbBookRotate;
    bool mbApplySalt;
    bool mbApplyHash;
    bool mbApplyEnc;
    bool mbBookExist;
    std::unique_ptr<IJsonServiceProvider> mJsonServiceProvider;

    Storage<std::unique_ptr<IMetadataUidTransform>> mMetadataUidTfStorage;
    std::vector<IMetadataUidTransform*> mMetadataUidTfs;
    DefaultMetadataUidTransform mDefaultUidTf;
};

MHTRPLUGIN_EXPORT(HPPJsonWriter)