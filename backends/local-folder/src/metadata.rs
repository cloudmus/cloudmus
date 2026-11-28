use id3::TagLike;
use lofty::config::{ParseOptions, ParsingMode};
use lofty::file::{AudioFile, FileType, TaggedFile, TaggedFileExt};
use lofty::mp4::{Mp4Codec, Mp4File};
use lofty::probe::Probe;
use lofty::tag::{Accessor, ItemKey};
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

#[derive(Default)]
pub struct Metadata {
    pub title: String,
    pub artists: Vec<String>,
    pub album: String,
    pub duration_ms: u64,
    pub codec: Option<String>,
    pub bitrate: Option<u32>,
    pub cover: Option<Vec<u8>>,
}

pub fn extension(path: &Path) -> String {
    path.extension()
        .and_then(|value| value.to_str())
        .unwrap_or("")
        .to_ascii_lowercase()
}

pub fn mime(extension: &str) -> Option<&'static str> {
    Some(match extension {
        "mp3" | "mp2" => "audio/mpeg",
        "flac" => "audio/flac",
        "ogg" | "oga" | "opus" | "spx" => "audio/ogg",
        "m4a" | "m4b" => "audio/mp4",
        "wav" => "audio/wav",
        "wma" => "audio/x-ms-wma",
        "aac" => "audio/aac",
        "webm" | "weba" => "audio/webm",
        "mka" => "audio/x-matroska",
        "aif" | "aiff" | "aifc" => "audio/aiff",
        "ape" => "audio/x-ape",
        "wv" => "audio/x-wavpack",
        "mpc" => "audio/x-musepack",
        "tta" => "audio/x-tta",
        "dsf" => "audio/x-dsf",
        "dff" => "audio/x-dff",
        "ac3" => "audio/ac3",
        "eac3" => "audio/eac3",
        _ => return None,
    })
}

pub fn read(path: &Path, cover: bool) -> Metadata {
    let ext = extension(path);
    let mut out = Metadata::default();
    if matches!(ext.as_str(), "webm" | "weba" | "mka") {
        if let Ok(mut file) = File::open(path) {
            let _ = crate::matroska::read(&mut file, &mut out, cover);
            estimate_bitrate(path, &mut out);
        }
        return out;
    }
    let options = ParseOptions::new()
        .parsing_mode(ParsingMode::Relaxed)
        .read_cover_art(cover);
    let mp4 = if matches!(ext.as_str(), "m4a" | "m4b") {
        File::open(path)
            .ok()
            .and_then(|mut file| Mp4File::read_from(&mut file, options).ok())
    } else {
        None
    };
    let mp4_codec = mp4.as_ref().map(|file| match file.properties().codec() {
        Mp4Codec::ALAC => "alac",
        Mp4Codec::FLAC => "flac",
        Mp4Codec::MP3 => "mp3",
        _ => "aac",
    });
    let audio = mp4.map(TaggedFile::from).map_or_else(
        || {
            Probe::open(path)
                .and_then(|probe| probe.guess_file_type().map_err(Into::into))
                .and_then(|probe| probe.options(options).read())
        },
        Ok,
    );
    if let Ok(audio) = audio {
        let codec = match audio.file_type() {
            FileType::Mpeg if ext == "mp2" => "mp2",
            FileType::Mpeg => "mp3",
            FileType::Aac => "aac",
            FileType::Mp4 => mp4_codec.unwrap_or("aac"),
            FileType::Aiff | FileType::Wav => "pcm",
            FileType::Ape => "ape",
            FileType::Flac => "flac",
            FileType::Mpc => "musepack",
            FileType::Opus => "opus",
            FileType::Vorbis => "vorbis",
            FileType::Speex => "speex",
            FileType::WavPack => "wavpack",
            _ => &ext,
        };
        out.codec = Some(codec.to_owned());
        out.duration_ms = audio.properties().duration().as_millis() as u64;
        out.bitrate = audio
            .properties()
            .audio_bitrate()
            .filter(|value| *value > 0);
        if let Some(tag) = audio.primary_tag().or_else(|| audio.first_tag()) {
            out.title = tag
                .title()
                .map(|value| value.into_owned())
                .unwrap_or_default();
            out.artists = tag
                .get_strings(ItemKey::TrackArtist)
                .map(str::to_owned)
                .collect();
            out.album = tag
                .album()
                .map(|value| value.into_owned())
                .unwrap_or_default();
            out.cover = tag
                .pictures()
                .first()
                .filter(|picture| picture.data().len() <= 32 * 1024 * 1024)
                .map(|picture| picture.data().to_vec());
        }
    } else {
        out.codec = Some(
            match ext.as_str() {
                "dsf" | "dff" => "dsd",
                "wav" | "aif" | "aiff" | "aifc" => "pcm",
                _ => &ext,
            }
            .to_owned(),
        );
        if let Ok(mut file) = File::open(path) {
            let _ = read_other(&mut file, &ext, &mut out, cover);
            if matches!(ext.as_str(), "dsf" | "dff" | "wma" | "tta") {
                estimate_bitrate(path, &mut out);
            }
        }
    }
    out
}

fn estimate_bitrate(path: &Path, out: &mut Metadata) {
    if out.duration_ms > 0 && out.bitrate.is_none() {
        if let Ok(info) = path.metadata() {
            out.bitrate = u32::try_from(
                info.len()
                    .saturating_mul(8)
                    .saturating_add(out.duration_ms / 2)
                    / out.duration_ms,
            )
            .ok()
            .filter(|value| *value > 0);
        }
    }
}

fn read_other(file: &mut File, ext: &str, out: &mut Metadata, cover: bool) -> std::io::Result<()> {
    match ext {
        "tta" => {
            let mut head = [0; 22];
            file.read_exact(&mut head[..10])?;
            if &head[..3] == b"ID3" && head[6..10].iter().all(|byte| *byte < 128) {
                let size = head[6..10]
                    .iter()
                    .fold(0, |size, byte| (size << 7) | u64::from(*byte));
                let end = 10 + size + if head[5] & 0x10 != 0 { 10 } else { 0 };
                read_id3(file, 0, end, out, cover);
                file.seek(SeekFrom::Start(end))?;
            } else {
                file.rewind()?;
            }
            file.read_exact(&mut head)?;
            if &head[..4] == b"TTA1" {
                let rate = u32::from_le_bytes(head[10..14].try_into().unwrap()) as u64;
                let samples = u32::from_le_bytes(head[14..18].try_into().unwrap()) as u64;
                out.duration_ms = (samples * 1000).checked_div(rate).unwrap_or(0);
            }
        }
        "dsf" => {
            let mut head = [0; 64];
            file.read_exact(&mut head)?;
            if &head[..4] == b"DSD " && &head[28..32] == b"fmt " {
                let rate = u32::from_le_bytes(head[56..60].try_into().unwrap()) as u64;
                let count = &mut [0; 8];
                file.read_exact(count)?;
                out.duration_ms = u64::from_le_bytes(*count)
                    .saturating_mul(1000)
                    .checked_div(rate)
                    .unwrap_or(0);
                let tag = u64::from_le_bytes(head[20..28].try_into().unwrap());
                if tag != 0 {
                    let length = file.metadata()?.len().saturating_sub(tag);
                    read_id3(file, tag, length, out, cover);
                }
            }
        }
        "dff" => read_dff(file, out, cover)?,
        "wma" => read_asf(file, out)?,
        _ => {}
    }
    Ok(())
}

fn read_id3(file: &mut File, position: u64, length: u64, out: &mut Metadata, cover: bool) {
    if length < 10 || file.seek(SeekFrom::Start(position)).is_err() {
        return;
    }
    let mut header = [0; 10];
    if file.read_exact(&mut header).is_err()
        || &header[..3] != b"ID3"
        || header[6..].iter().any(|byte| *byte >= 128)
    {
        return;
    }
    let size = header[6..]
        .iter()
        .fold(0, |size, byte| (size << 7) | u64::from(*byte));
    if size > 32 * 1024 * 1024 || 10 + size > length {
        return;
    }
    let mut bytes = header.to_vec();
    bytes.resize(10 + size as usize, 0);
    if file.read_exact(&mut bytes[10..]).is_err() {
        return;
    }
    if let Ok(tag) = id3::Tag::read_from2(std::io::Cursor::new(bytes)) {
        out.title = tag.title().unwrap_or("").to_owned();
        out.artists = tag
            .artists()
            .unwrap_or_default()
            .into_iter()
            .map(str::to_owned)
            .collect();
        out.album = tag.album().unwrap_or("").to_owned();
        if cover {
            out.cover = tag.pictures().next().map(|picture| picture.data.clone());
        }
    }
}

fn dff_chunk(file: &mut File, boundary: u64) -> std::io::Result<([u8; 4], u64, u64)> {
    let mut header = [0; 12];
    file.read_exact(&mut header)?;
    let length = u64::from_be_bytes(header[4..].try_into().unwrap());
    let end = file.stream_position()?.saturating_add(length);
    if end > boundary {
        return Err(std::io::ErrorKind::InvalidData.into());
    }
    Ok((header[..4].try_into().unwrap(), length, end))
}

fn read_dff(file: &mut File, out: &mut Metadata, cover: bool) -> std::io::Result<()> {
    let file_size = file.metadata()?.len();
    let (id, length, end) = dff_chunk(file, file_size)?;
    let mut kind = [0; 4];
    file.read_exact(&mut kind)?;
    if &id != b"FRM8" || length < 4 || &kind != b"DSD " {
        return Ok(());
    }
    let (mut rate, mut channels, mut bytes, mut dst_duration) = (0u64, 0u64, 0u64, 0u64);
    while file.stream_position()?.saturating_add(12) <= end {
        let (kind, length, next) = dff_chunk(file, end)?;
        match &kind {
            b"PROP" if length >= 4 => {
                let mut prop = [0; 4];
                file.read_exact(&mut prop)?;
                if &prop == b"SND " {
                    while file.stream_position()?.saturating_add(12) <= next {
                        let (kind, length, stop) = dff_chunk(file, next)?;
                        if &kind == b"FS  " && length >= 4 {
                            let mut value = [0; 4];
                            file.read_exact(&mut value)?;
                            rate = u64::from(u32::from_be_bytes(value));
                        } else if &kind == b"CHNL" && length >= 2 {
                            let mut value = [0; 2];
                            file.read_exact(&mut value)?;
                            channels = u64::from(u16::from_be_bytes(value));
                        }
                        file.seek(SeekFrom::Start(stop + (length & 1)))?;
                    }
                }
            }
            b"DSD " => bytes = length,
            b"DST " if length >= 18 => {
                let (kind, length, _) = dff_chunk(file, next)?;
                if &kind == b"FRTE" && length >= 6 {
                    let mut value = [0; 6];
                    file.read_exact(&mut value)?;
                    let frames = u64::from(u32::from_be_bytes(value[..4].try_into().unwrap()));
                    let frame_rate = u64::from(u16::from_be_bytes(value[4..].try_into().unwrap()));
                    dst_duration = (frames * 1000).checked_div(frame_rate).unwrap_or(0);
                }
            }
            b"ID3 " => {
                let position = file.stream_position()?;
                read_id3(file, position, length, out, cover);
            }
            _ => {}
        }
        file.seek(SeekFrom::Start(next + (length & 1)))?;
    }
    out.duration_ms = if dst_duration > 0 {
        dst_duration
    } else {
        bytes
            .saturating_mul(8000)
            .checked_div(rate * channels)
            .unwrap_or(0)
    };
    Ok(())
}

fn utf16(bytes: &[u8]) -> String {
    String::from_utf16_lossy(
        &bytes
            .chunks_exact(2)
            .map(|pair| u16::from_le_bytes([pair[0], pair[1]]))
            .collect::<Vec<_>>(),
    )
    .trim_matches('\0')
    .to_owned()
}

fn read_asf(file: &mut File, out: &mut Metadata) -> std::io::Result<()> {
    const HEADER: [u8; 16] = [
        0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11, 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce,
        0x6c,
    ];
    const PROPERTIES: [u8; 16] = [
        0xa1, 0xdc, 0xab, 0x8c, 0x47, 0xa9, 0xcf, 0x11, 0x8e, 0xe4, 0x00, 0xc0, 0x0c, 0x20, 0x53,
        0x65,
    ];
    const CONTENT: [u8; 16] = [
        0x33, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11, 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce,
        0x6c,
    ];
    const EXTENDED: [u8; 16] = [
        0x40, 0xa4, 0xd0, 0xd2, 0x07, 0xe3, 0xd2, 0x11, 0x97, 0xf0, 0x00, 0xa0, 0xc9, 0x5e, 0xa8,
        0x50,
    ];
    let mut header = [0; 30];
    file.read_exact(&mut header)?;
    if header[..16] != HEADER {
        return Ok(());
    }
    let end = u64::from_le_bytes(header[16..24].try_into().unwrap()).min(file.metadata()?.len());
    while file.stream_position()? + 24 <= end {
        let mut object = [0; 24];
        file.read_exact(&mut object)?;
        let id: [u8; 16] = object[..16].try_into().unwrap();
        let size = u64::from_le_bytes(object[16..].try_into().unwrap()).saturating_sub(24);
        if size == 0 || size > end - file.stream_position()? {
            break;
        }
        if matches!(id, PROPERTIES | CONTENT | EXTENDED) && size <= 1024 * 1024 {
            let mut data = vec![0; size as usize];
            file.read_exact(&mut data)?;
            match id {
                PROPERTIES if data.len() >= 80 => {
                    let duration = u64::from_le_bytes(data[40..48].try_into().unwrap()) / 10_000;
                    let preroll = u64::from_le_bytes(data[56..64].try_into().unwrap());
                    out.duration_ms = duration.saturating_sub(preroll);
                    out.bitrate = Some(u32::from_le_bytes(data[76..80].try_into().unwrap()) / 1000)
                        .filter(|value| *value > 0);
                }
                CONTENT if data.len() >= 10 => {
                    let title = u16::from_le_bytes(data[..2].try_into().unwrap()) as usize;
                    let author = u16::from_le_bytes(data[2..4].try_into().unwrap()) as usize;
                    if let Some(bytes) = data.get(10..10 + title) {
                        out.title = utf16(bytes);
                    }
                    if let Some(bytes) = data.get(10 + title..10 + title + author) {
                        let artist = utf16(bytes);
                        if !artist.is_empty() {
                            out.artists.push(artist);
                        }
                    }
                }
                EXTENDED => {
                    let mut data = data.as_slice();
                    let Some(count) = read_u16(&mut data) else {
                        continue;
                    };
                    for _ in 0..count {
                        let Some(name_len) = read_u16(&mut data) else {
                            break;
                        };
                        let Some((name, tail)) = data.split_at_checked(name_len as usize) else {
                            break;
                        };
                        data = tail;
                        let Some(kind) = read_u16(&mut data) else {
                            break;
                        };
                        let Some(len) = read_u16(&mut data) else {
                            break;
                        };
                        let Some((value, tail)) = data.split_at_checked(len as usize) else {
                            break;
                        };
                        data = tail;
                        if kind == 0 && utf16(name) == "WM/AlbumTitle" {
                            out.album = utf16(value);
                        }
                    }
                }
                _ => {}
            }
        } else {
            file.seek(SeekFrom::Current(size as i64))?;
        }
    }
    Ok(())
}

fn read_u16(data: &mut &[u8]) -> Option<u16> {
    let (value, tail) = data.split_at_checked(2)?;
    *data = tail;
    Some(u16::from_le_bytes(value.try_into().unwrap()))
}
