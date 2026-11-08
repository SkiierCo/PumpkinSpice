#include "wiiumenu/ps.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <optional>

#include "wiiumenu/bps.hpp"
#include "wiiumenu/sarc.hpp"
#include "wiiumenu/yaz0.hpp"

namespace wiiumenu::ps {
namespace {

std::string crc_hex(ByteView data) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", unsigned(bps::crc32(data)));
    return buf;
}

bool equal_bytes(ByteView a, ByteView b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

// The SARC inside a file (decompressed if it is Yaz0).
Bytes payload(ByteView data) {
    return yaz0::is_yaz0(data) ? yaz0::decompress(data) : Bytes(data.begin(), data.end());
}

bool is_container(ByteView data) {
    if (yaz0::is_yaz0(data)) {
        try {
            return has_magic(yaz0::decompress(data, 4), "SARC");
        } catch (const FormatError&) {
            return false;
        }
    }
    return has_magic(data, "SARC");
}

json::Value blob_ref(std::uint64_t offset, std::uint64_t size) {
    return json::Array{json::Value::integer((long long) offset), json::Value::integer((long long) size)};
}

Bytes read_blob(const json::Value& node, const BlobReader& blob) {
    const auto [off, size] = blob_range(node);
    return blob(off, size);
}

void check_crc(const json::Value& node, ByteView stock, const std::string& name) {
    if (const json::Value* c = node.find("stock_crc"))
        if (c->string() != crc_hex(stock))
            throw FormatError(name + ": the stock file is not the one this package was made from (CRC " + crc_hex(stock) +
                              ", expected " + c->string() + ")");
}

// Applies an archive node's children to `data` (SARC or Yaz0 SARC), with tree::build's rules.
// Memory: on the console a Menu archive and copies of it don't fit (Men2.pack holds a 23 MB Yaz0 archive). The members
// are views into the stock archive, never copies, and only changed members are held on their own. A Yaz0 archive is
// never decompressed whole: it is read as a stream (Yaz0 refers back only 4 KiB), once for the stock members the
// children change, and again while the rebuilt archive goes straight from its members into the compressor.
Bytes apply_archive(const json::Value& node, ByteView data, const BlobReader& blob) {
    const bool yaz = yaz0::is_yaz0(data);
    sarc::ArchiveView s;
    std::size_t original_size = data.size();
    if (yaz) {
        yaz0::Stream in(data);
        original_size = in.size();
        Bytes head(std::min<std::size_t>(0x14, original_size));
        in.read(head.data(), head.size());
        if (!has_magic(head, "SARC") || head.size() < 0x14) throw FormatError("not a SARC archive");
        const std::size_t doff = read_u32(head, 12, read_u16(head, 6, true) == 0xFEFF);
        if (doff < head.size() || doff > original_size) throw FormatError("SARC data offset out of range");
        head.resize(doff);
        in.read(head.data() + 0x14, doff - 0x14);
        s = sarc::ArchiveView::parse_index(head, original_size);
    } else {
        s = sarc::ArchiveView::parse(data);
    }

    std::vector<Bytes> owned;  // members' data read or changed here (s.files point into it; moving a Bytes keeps its data)
    auto keep = [&](Bytes d) -> ByteView {
        owned.push_back(std::move(d));
        return owned.back();
    };
    if (yaz) {  // the stock members the children patch, read in archive order
        std::vector<sarc::FileView*> needed;
        for (const json::Value& child : node.at("children").array()) {
            const std::string& op = child.at("op").string();
            if (op != "patch" && op != "archive") continue;
            for (sarc::FileView& f : s.files)
                if (f.name == child.at("path").string() && f.indexed) needed.push_back(&f);
        }
        std::sort(needed.begin(), needed.end(), [](const sarc::FileView* a, const sarc::FileView* b) { return a->offset < b->offset; });
        needed.erase(std::unique(needed.begin(), needed.end()), needed.end());
        std::optional<yaz0::Stream> in;
        for (sarc::FileView* f : needed) {
            if (!in || in->position() > f->offset) in.emplace(data);  // (members that overlap: from the start again)
            in->skip(f->offset - in->position());
            Bytes d(f->size);
            in->read(d.data(), d.size());
            f->data = keep(std::move(d));
            f->indexed = false;
        }
    }

    for (const json::Value& child : node.at("children").array()) {
        const std::string& name = child.at("path").string();
        const std::string& op = child.at("op").string();
        auto it = std::find_if(s.files.begin(), s.files.end(), [&](const sarc::FileView& f) { return f.name == name; });
        if (op == "remove") {
            if (it != s.files.end()) s.files.erase(it);
            continue;
        }
        if (op == "add") {
            const ByteView d = keep(read_blob(child, blob));
            const std::uint32_t a = sarc::default_alignment(name, d);
            if (it != s.files.end()) {
                it->data = d;
                it->indexed = false;
                it->alignment = a;
            } else {
                s.files.push_back({name, d, a});
            }
            continue;
        }
        if (it == s.files.end()) throw FormatError(name + ": member missing from the stock archive");
        check_crc(child, it->data, name);
        Bytes d;
        if (op == "patch") {
            d = bps::apply(read_blob(child, blob), it->data);
        } else if (op == "archive") {
            d = apply_archive(child, it->data, blob);
        } else {
            throw FormatError(name + ": unknown op \"" + op + "\"");
        }
        if (!equal_bytes(d, it->data)) {
            it->alignment = sarc::default_alignment(name, d);  // changed: what the data needs
            const ByteView old = it->data;
            it->data = keep(std::move(d));
            for (Bytes& b : owned) {  // the stock member read above is not needed any more
                if (!b.empty() && b.data() == old.data()) {
                    Bytes().swap(b);
                    break;
                }
            }
        }
    }
    const sarc::Layout out = sarc::layout(s);
    if (!yaz) return out.join();

    // the rebuilt archive's bytes in order, the unchanged members streamed from the stock archive
    const auto segments = out.segments();
    std::size_t seg = 0, at = 0;  // where the reader is in `segments`
    std::optional<yaz0::Stream> stock;
    const yaz0::Reader read = [&](std::uint8_t* dst, std::size_t n) {
        while (n) {
            const sarc::Layout::Segment& sg = segments[seg];
            const std::size_t k = std::min(n, sg.size - at);
            if (sg.indexed) {
                const std::size_t from = sg.offset + at;
                if (!stock || stock->position() > from) stock.emplace(data);
                stock->skip(from - stock->position());
                stock->read(dst, k);
            } else if (sg.data) {
                std::memcpy(dst, sg.data + at, k);
            } else {
                std::memset(dst, 0, k);
            }
            dst += k;
            n -= k;
            at += k;
            if (at == sg.size) {
                ++seg;
                at = 0;
            }
        }
    };

    // untouched contents: the stock compressed bytes (compared by size and CRC32)
    if (out.size == original_size) {
        Bytes chunk(0x10000);
        std::uint32_t rebuilt = 0, original = 0;
        yaz0::Stream in(data);
        for (std::size_t left = out.size; left;) {
            const std::size_t n = std::min(left, chunk.size());
            read(chunk.data(), n);
            rebuilt = bps::crc32(ByteView(chunk.data(), n), rebuilt);
            in.read(chunk.data(), n);
            original = bps::crc32(ByteView(chunk.data(), n), original);
            left -= n;
        }
        if (rebuilt == original) return Bytes(data.begin(), data.end());
        seg = at = 0;  // read again from the start for the compressor
        stock.reset();
    }
    return yaz0::compress(out.size, read, yaz0::header_alignment(data));
}

struct Builder {
    Bytes data;  // the data area
    Report& report;

    json::Value add_blob(const std::string& path, const char* op, ByteView bytes) {
        json::Value n = json::Object{};
        n.set("path", path);
        n.set("op", op);
        n.set("data", blob_ref(data.size(), bytes.size()));
        data.insert(data.end(), bytes.begin(), bytes.end());
        return n;
    }

    // node turning `stock` into `theme`, or nullopt if they have the same contents
    std::optional<json::Value> diff(const std::string& path, ByteView stock, ByteView theme) {
        if (equal_bytes(stock, theme)) return std::nullopt;
        if (is_container(stock) && is_container(theme)) {
            const Bytes ps = payload(stock), pt = payload(theme);
            if (equal_bytes(ps, pt)) return std::nullopt;  // only the compression differs
            const sarc::Archive sa = sarc::Archive::parse(ps), sb = sarc::Archive::parse(pt);
            json::Array children;
            for (const sarc::File& f : sb.files) {
                if (const sarc::File* sf = sa.get(f.name)) {
                    if (auto c = diff(f.name, sf->data, f.data)) children.push_back(std::move(*c));
                } else {
                    children.push_back(add_blob(f.name, "add", f.data));
                    ++report.member_adds;
                }
            }
            for (const sarc::File& f : sa.files)
                if (!sb.get(f.name)) {
                    json::Value n = json::Object{};
                    n.set("path", f.name);
                    n.set("op", "remove");
                    children.push_back(std::move(n));
                    ++report.removes;
                }
            if (children.empty()) return std::nullopt;  // same members, only their layout differs
            json::Value n = json::Object{};
            n.set("path", path);
            n.set("op", "archive");
            n.set("stock_crc", crc_hex(stock));
            n.set("children", std::move(children));
            return n;
        }
        json::Value n = add_blob(path, "patch", bps::create(stock, theme));
        n.set("stock_crc", crc_hex(stock));
        ++report.patches;
        return n;
    }

    BlobReader reader() {
        return [this](std::uint64_t off, std::uint64_t size) {
            if (off + size > data.size()) throw FormatError("blob out of range");
            return Bytes(data.begin() + std::ptrdiff_t(off), data.begin() + std::ptrdiff_t(off + size));
        };
    }
};

}  // namespace

std::uint32_t manifest_size(ByteView header) {
    if (header.size() < kHeaderSize || !has_magic(header, "PSPK")) throw FormatError("not a PumpkinSpice theme package");
    const std::uint32_t version = read_u32(header, 4, true);
    if (version != kVersion) throw FormatError("theme package format version " + std::to_string(version) + " is not supported");
    return read_u32(header, 8, true);
}

Manifest read_manifest(ByteView head) {
    const std::uint32_t size = manifest_size(head);
    if (head.size() < kHeaderSize + size) throw FormatError("theme package manifest is truncated");
    Manifest m;
    m.doc = json::parse(std::string_view(reinterpret_cast<const char*>(head.data() + kHeaderSize), size));
    m.data_start = kHeaderSize + size;
    return m;
}

std::pair<std::uint64_t, std::uint64_t> blob_range(const json::Value& node) {
    const json::Array& r = node.at("data").array();
    if (r.size() != 2) throw FormatError("bad data reference");
    return {std::uint64_t(r[0].as_int()), std::uint64_t(r[1].as_int())};
}

std::string region_of(const std::string& p) {
    // a language folder anywhere in the path: Eu/Us/Jp + a capitalised language name (EuEnglish, UsFrench, ...)
    for (std::size_t s = 0; s < p.size();) {
        std::size_t e = p.find('/', s);
        if (e == std::string::npos) e = p.size();
        const std::string seg = p.substr(s, e - s);
        if (seg.size() > 3 && std::isupper(static_cast<unsigned char>(seg[2])) && std::islower(static_cast<unsigned char>(seg[3]))) {
            const std::string pre = seg.substr(0, 2);
            if (pre == "Eu") return "EUR";
            if (pre == "Us") return "USA";
            if (pre == "Jp") return "JPN";
        }
        s = e + 1;
    }
    if (p.rfind("titles/", 0) == 0 && p.size() > 7 + 17) {
        const std::string high = p.substr(7, 8);
        if (high == "00050010" || high == "00050030") {
            switch (p[7 + 9 + 5]) {
                case '0': return "JPN";
                case '1': return "USA";
                case '2': return "EUR";
                default: break;
            }
        }
    }
    return "";
}

bool for_region(const json::Value& node, const std::string& region) {
    const json::Value* r = node.find("regions");
    if (!r) return true;
    for (const json::Value& v : r->array())
        if (v.string() == region) return true;
    return false;
}

std::string console_path(const std::string& p) {
    if (p.rfind("content/", 0) == 0) return "/vol/" + p;
    if (p.rfind("titles/", 0) == 0 && p.size() > 7 + 18 && p[7 + 8] == '/' && p[7 + 17] == '/') {
        const std::string high = p.substr(7, 8);
        std::string upper = high;
        for (auto& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
        const bool system = upper.rfind("0005001", 0) == 0 || upper == "00050030";
        return std::string("/vol/storage_mlc01/") + (system ? "sys" : "usr") + "/title/" + p.substr(7);
    }
    return "";
}

std::optional<Bytes> rebuild(const json::Value& node, const BlobReader& blob, const Bytes* stock, std::string* why) {
    const std::string& op = node.at("op").string();
    const std::string& path = node.at("path").string();
    if (op == "add") return read_blob(node, blob);
    if (!stock) {
        if (why) *why = "no stock file on this console";
        return std::nullopt;
    }
    try {
        check_crc(node, *stock, path);
        if (op == "patch") return bps::apply(read_blob(node, blob), *stock);
        if (op == "archive") return apply_archive(node, *stock, blob);
    } catch (const FormatError& e) {
        if (why) *why = e.what();
        return std::nullopt;
    }
    throw FormatError(path + ": unknown op \"" + op + "\"");
}

bool same_content(ByteView a, ByteView b) {
    if (equal_bytes(a, b)) return true;
    if (!is_container(a) || !is_container(b)) return false;
    const Bytes pa = payload(a), pb = payload(b);
    if (equal_bytes(pa, pb)) return true;
    const sarc::Archive sa = sarc::Archive::parse(pa), sb = sarc::Archive::parse(pb);
    if (sa.files.size() != sb.files.size()) return false;
    for (const sarc::File& f : sa.files) {
        const sarc::File* g = sb.get(f.name);
        if (!g || !same_content(f.data, g->data)) return false;
    }
    return true;
}

Bytes create(const std::string& name, const std::string& code, const std::vector<Entry>& entries, Report& report) {
    Builder b{{}, report};
    json::Array files;
    for (const Entry& e : entries) {
        if (e.stocks.empty()) {
            files.push_back(b.add_blob(e.path, "add", e.theme));
            report.added.push_back(e.path);
            continue;
        }
        // one node per stock version (a console uses the one whose stock_crc matches its file)
        bool any = false;
        for (const Bytes& stock : e.stocks) {
            const std::size_t mark = b.data.size();
            std::optional<json::Value> node = b.diff(e.path, stock, e.theme);
            if (!node) continue;  // this stock version already is the theme's file: nothing to rebuild
            // check: rebuilding the node from stock must give the theme's contents
            std::string why;
            const std::optional<Bytes> back = rebuild(*node, b.reader(), &stock, &why);
            if (!back || !same_content(*back, e.theme)) {
                b.data.resize(mark);  // drop this node's blobs: store the file as one patch instead
                node = b.add_blob(e.path, "patch", bps::create(stock, e.theme));
                node->set("stock_crc", crc_hex(stock));
                report.whole.push_back(e.path + (back ? "" : " (" + why + ")"));
            }
            files.push_back(std::move(*node));
            any = true;
        }
        if (!any) report.unchanged.push_back(e.path);
    }
    for (json::Value& n : files) {
        const std::string region = region_of(n.at("path").string());
        if (!region.empty()) n.set("regions", json::Array{json::Value(region)});
    }
    json::Value m = json::Object{};
    m.set("format", "PumpkinSpice theme package");
    m.set("version", json::Value::integer(kVersion));
    m.set("name", name);
    if (!code.empty()) m.set("code", code);
    m.set("files", std::move(files));
    const std::string text = json::dump(m);

    Bytes out;
    out.insert(out.end(), {'P', 'S', 'P', 'K'});
    put_u32(out, kVersion, true);
    put_u32(out, std::uint32_t(text.size()), true);
    out.insert(out.end(), text.begin(), text.end());
    out.insert(out.end(), b.data.begin(), b.data.end());
    return out;
}

}  // namespace wiiumenu::ps
